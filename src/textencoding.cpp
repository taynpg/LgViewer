#include "textencoding.h"

#include <QStringDecoder>
#include <QtGlobal>

namespace lv {

QString encodingLabel(TextEncoding enc)
{
    switch (enc) {
    case TextEncoding::Auto:    return QStringLiteral("自动");
    case TextEncoding::Utf8:    return QStringLiteral("UTF-8");
    case TextEncoding::Utf8Bom: return QStringLiteral("UTF-8 BOM");
    case TextEncoding::Gbk:     return QStringLiteral("GBK/GB18030");
    case TextEncoding::Utf16LE: return QStringLiteral("UTF-16 LE");
    case TextEncoding::Utf16BE: return QStringLiteral("UTF-16 BE");
    case TextEncoding::Latin1:  return QStringLiteral("Latin-1");
    }
    return QStringLiteral("?");
}

// 严格 UTF-8 校验，返回非法序列个数
static qint64 countInvalidUtf8(const uchar *p, qint64 n)
{
    qint64 i = 0;
    qint64 bad = 0;
    while (i < n) {
        const uchar c = p[i];
        if (c < 0x80) {
            ++i;
            continue;
        }
        int len = 0;
        quint32 cp = 0;
        if ((c & 0xE0) == 0xC0) {
            len = 2;
            cp = c & 0x1Fu;
        } else if ((c & 0xF0) == 0xE0) {
            len = 3;
            cp = c & 0x0Fu;
        } else if ((c & 0xF8) == 0xF0) {
            len = 4;
            cp = c & 0x07u;
        } else {
            ++bad;
            ++i;
            continue;
        }
        if (i + len > n) { // 样本被截断，视为不完整而不是非法
            break;
        }
        bool ok = true;
        for (int k = 1; k < len; ++k) {
            const uchar cc = p[i + k];
            if ((cc & 0xC0) != 0x80) {
                ok = false;
                break;
            }
            cp = (cp << 6) | (cc & 0x3Fu);
        }
        if (!ok) {
            ++bad;
            ++i;
            continue;
        }
        if ((len == 2 && cp < 0x80) || (len == 3 && cp < 0x800) || (len == 4 && cp < 0x10000)
            || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
            ++bad;
            ++i;
            continue;
        }
        i += len;
    }
    return bad;
}

// GB18030 结构校验（只看字节形态，不看码位分配）
static bool isValidGb18030(const uchar *p, qint64 n)
{
    qint64 i = 0;
    while (i < n) {
        const uchar c = p[i];
        if (c < 0x80) {
            ++i;
            continue;
        }
        if (c == 0x80) { // CP936 里 0x80 映射到 €
            ++i;
            continue;
        }
        if (c < 0x81 || c > 0xFE)
            return false;
        if (i + 1 >= n)
            return true; // 结尾截断
        const uchar c2 = p[i + 1];
        if (c2 >= 0x40 && c2 <= 0xFE && c2 != 0x7F) {
            i += 2;
            continue;
        }
        if (c2 >= 0x30 && c2 <= 0x39) {
            if (i + 2 < n) {
                const uchar c3 = p[i + 2];
                if (c3 >= 0x81 && c3 <= 0xFE) {
                    if (i + 3 < n) {
                        const uchar c4 = p[i + 3];
                        if (c4 >= 0x30 && c4 <= 0x39) {
                            i += 4;
                            continue;
                        }
                        return false;
                    }
                    return true;
                }
                return false;
            }
            return true;
        }
        return false;
    }
    return true;
}

TextEncoding detectEncoding(const char *data, qint64 size)
{
    if (!data || size <= 0)
        return TextEncoding::Utf8;

    const uchar *p = reinterpret_cast<const uchar *>(data);

    // 1) BOM
    if (size >= 3 && p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF)
        return TextEncoding::Utf8Bom;
    if (size >= 2 && p[0] == 0xFF && p[1] == 0xFE)
        return TextEncoding::Utf16LE;
    if (size >= 2 && p[0] == 0xFE && p[1] == 0xFF)
        return TextEncoding::Utf16BE;

    // 2) 采样：取前 256KB，并在换行处截断，避免半截多字节序列误判
    qint64 sample = qMin<qint64>(size, 256 * 1024);
    while (sample > 0 && sample < size) {
        if (p[sample - 1] == '\n')
            break;
        --sample;
    }
    if (sample <= 0)
        sample = qMin<qint64>(size, 256 * 1024);

    // 3) 无 BOM 的 UTF-16 粗略判断：零字节集中在奇/偶位置
    if (sample >= 16) {
        qint64 zeroEven = 0, zeroOdd = 0;
        for (qint64 i = 0; i < sample; ++i) {
            if (p[i] == 0) {
                if (i & 1)
                    ++zeroOdd;
                else
                    ++zeroEven;
            }
        }
        const qint64 half = sample / 2;
        if (half > 0 && (zeroOdd * 100 / half) > 25 && (zeroEven * 100 / half) < 5)
            return TextEncoding::Utf16LE;
        if (half > 0 && (zeroEven * 100 / half) > 25 && (zeroOdd * 100 / half) < 5)
            return TextEncoding::Utf16BE;
    }

    // 4) UTF-8 优先（纯 ASCII 也走这里）
    if (countInvalidUtf8(p, sample) == 0)
        return TextEncoding::Utf8;

    // 5) GB18030
    if (isValidGb18030(p, sample))
        return TextEncoding::Gbk;

    return TextEncoding::Latin1;
}

bool needsConversion(TextEncoding enc)
{
    return enc != TextEncoding::Utf8 && enc != TextEncoding::Utf8Bom;
}

static QString decoderName(TextEncoding enc)
{
    switch (enc) {
    case TextEncoding::Gbk:     return QStringLiteral("GB18030");
    case TextEncoding::Utf16LE: return QStringLiteral("UTF-16LE");
    case TextEncoding::Utf16BE: return QStringLiteral("UTF-16BE");
    case TextEncoding::Latin1:  return QStringLiteral("ISO-8859-1");
    default:                    return QStringLiteral("UTF-8");
    }
}

bool convertToUtf8(QByteArray &out, const char *data, qint64 size, TextEncoding enc,
                   QString *error)
{
    out.clear();
    if (size <= 0)
        return true;

    QStringDecoder dec(decoderName(enc).toLatin1().constData());
    if (!dec.isValid()) {
        if (error)
            *error = QStringLiteral("系统不支持编码 %1").arg(encodingLabel(enc));
        return false;
    }

    // UTF-16 一个码元 2 字节，块大小取偶数字节，避免跨块半个码元
    qint64 chunk = 1024 * 1024;
    if (enc == TextEncoding::Utf16LE || enc == TextEncoding::Utf16BE)
        chunk &= ~qint64(1);

    out.reserve(int(qMin<qint64>(size * 3 / 2 + 16, 512LL * 1024 * 1024)));
    qint64 off = 0;
    while (off < size) {
        qint64 n = qMin(chunk, size - off);
        const QString s = dec(QByteArrayView(data + off, int(n)));
        const QByteArray u8 = s.toUtf8();
        out.append(u8);
        off += n;
        if (dec.hasError()) {
            // 有无法解码的字节（已用替换字符顶替），继续，不中断
        }
    }
    const QString tail = dec(QByteArrayView());
    if (!tail.isEmpty())
        out.append(tail.toUtf8());
    return true;
}

} // namespace lv
