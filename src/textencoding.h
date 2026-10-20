#pragma once

#include <QByteArray>
#include <QString>

namespace lv {

enum class TextEncoding {
    Auto = -1,
    Utf8 = 0,
    Utf8Bom,
    Gbk,        // GBK / GB18030 / CP936
    Utf16LE,
    Utf16BE,
    Latin1
};

QString encodingLabel(TextEncoding enc);

// 基于 BOM + UTF-8 严格校验 + GB18030 结构校验的启发式识别
TextEncoding detectEncoding(const char *data, qint64 size);

// 是否需要转码成 UTF-8 才能给正则用
bool needsConversion(TextEncoding enc);

// 转成 UTF-8（大文件分块转，控制峰值内存）
bool convertToUtf8(QByteArray &out, const char *data, qint64 size, TextEncoding enc,
                   QString *error = nullptr);

} // namespace lv
