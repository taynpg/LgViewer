#include "regexengine.h"

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

RegexEngine::RegexEngine() = default;

RegexEngine::~RegexEngine()
{
    freeAll();
}

void RegexEngine::freeAll()
{
    if (m_matchData) {
        pcre2_match_data_free(static_cast<pcre2_match_data *>(m_matchData));
        m_matchData = nullptr;
    }
    if (m_jitStack) {
        pcre2_jit_stack_free(static_cast<pcre2_jit_stack *>(m_jitStack));
        m_jitStack = nullptr;
    }
    if (m_matchContext) {
        pcre2_match_context_free(static_cast<pcre2_match_context *>(m_matchContext));
        m_matchContext = nullptr;
    }
    if (m_code) {
        pcre2_code_free(static_cast<pcre2_code *>(m_code));
        m_code = nullptr;
    }
    m_captureCount = 0;
    m_jit = 0;
    m_ovector.clear();
    m_names.clear();
    m_nameNumbers.clear();
}

void RegexEngine::release()
{
    freeAll();
}

bool RegexEngine::compile(const QString &pattern, QString *error, int *errorOffset)
{
    freeAll();
    m_pattern = pattern;
    m_lastError.clear();

    if (pattern.isEmpty()) {
        m_lastError = QStringLiteral("表达式为空");
        if (error)
            *error = m_lastError;
        return false;
    }

    const QByteArray pat = pattern.toUtf8();
    int errcode = 0;
    PCRE2_SIZE erroff = 0;
    const uint32_t options = PCRE2_UTF | PCRE2_UCP;

    pcre2_code *code = pcre2_compile(reinterpret_cast<PCRE2_SPTR>(pat.constData()),
                                     PCRE2_SIZE(pat.size()), options, &errcode, &erroff,
                                     nullptr);
    if (!code) {
        PCRE2_UCHAR buf[256];
        pcre2_get_error_message(errcode, buf, sizeof(buf));
        m_lastError = QString::fromUtf8(reinterpret_cast<const char *>(buf));
        if (error)
            *error = m_lastError;
        if (errorOffset)
            *errorOffset = int(erroff);
        return false;
    }
    m_code = code;

    uint32_t capcount = 0;
    pcre2_pattern_info(code, PCRE2_INFO_CAPTURECOUNT, &capcount);
    m_captureCount = int(capcount);

    uint32_t nameCount = 0;
    pcre2_pattern_info(code, PCRE2_INFO_NAMECOUNT, &nameCount);
    if (nameCount > 0) {
        uint32_t entrySize = 0;
        PCRE2_SPTR nameTable = nullptr;
        pcre2_pattern_info(code, PCRE2_INFO_NAMEENTRYSIZE, &entrySize);
        pcre2_pattern_info(code, PCRE2_INFO_NAMETABLE, &nameTable);
        // 某些构建下返回的是相对偏移，做一次防御性修正
        const PCRE2_SPTR base = reinterpret_cast<PCRE2_SPTR>(code);
        if (nameTable < base)
            nameTable = base + reinterpret_cast<uintptr_t>(nameTable);

        for (uint32_t i = 0; i < nameCount; ++i) {
            const PCRE2_SPTR entry = nameTable + i * entrySize;
            const int num = (int(entry[0]) << 8) | int(entry[1]);
            const QString name = QString::fromUtf8(reinterpret_cast<const char *>(entry + 2));
            m_names.append(name);
            m_nameNumbers.append(num);
        }
    }

    m_jit = 0;
    if (pcre2_jit_compile(code, PCRE2_JIT_COMPLETE) == 0)
        m_jit = 1;
    else
        m_jit = -1;

    m_matchData = pcre2_match_data_create_from_pattern(code, nullptr);

    auto *mctx = pcre2_match_context_create(nullptr);
    if (mctx) {
        m_matchContext = mctx;
        pcre2_jit_stack *stack = pcre2_jit_stack_create(64 * 1024, 4 * 1024 * 1024, nullptr);
        if (stack) {
            m_jitStack = stack;
            pcre2_jit_stack_assign(mctx, nullptr, stack);
        }
        // 放宽回溯限制，避免长行 + 贪婪量词直接报错
        pcre2_set_match_limit(mctx, 1000000);
        pcre2_set_depth_limit(mctx, 10000);
    }

    return m_matchData != nullptr;
}

int RegexEngine::match(const char *subject, int length)
{
    return matchAt(subject, length, 0);
}

int RegexEngine::matchAt(const char *subject, int length, int startOffset)
{
    if (!m_code || !m_matchData)
        return PCRE2_ERROR_BADMODE;
    if (!subject || length < 0 || startOffset < 0 || startOffset > length)
        return PCRE2_ERROR_NULL;

    const int rc = pcre2_match(static_cast<pcre2_code *>(m_code),
                               reinterpret_cast<PCRE2_SPTR>(subject), PCRE2_SIZE(length),
                               PCRE2_SIZE(startOffset), 0,
                               static_cast<pcre2_match_data *>(m_matchData),
                               static_cast<pcre2_match_context *>(m_matchContext));
    if (rc <= 0)
        return rc;

    const PCRE2_SIZE *ov = pcre2_get_ovector_pointer(static_cast<pcre2_match_data *>(m_matchData));
    const int slotCount = (m_captureCount + 1) * 2;
    m_ovector.resize(slotCount);
    for (int i = 0; i < slotCount; ++i) {
        const PCRE2_SIZE v = ov[i];
        m_ovector[i] = (v == PCRE2_UNSET) ? -1 : int(v);
    }
    return 1;
}

int RegexEngine::groupIndex(const QString &name) const
{
    for (int i = 0; i < m_names.size(); ++i) {
        if (m_names.at(i).compare(name, Qt::CaseInsensitive) == 0)
            return m_nameNumbers.at(i);
    }
    return -1;
}

QStringList RegexEngine::groupNames() const
{
    return m_names;
}

bool RegexEngine::hasCapture(int index) const
{
    if (index < 0)
        return false;
    const int slot = index * 2;
    return slot + 1 < m_ovector.size() && m_ovector.at(slot) >= 0 && m_ovector.at(slot + 1) >= 0;
}

int RegexEngine::start(int index) const
{
    if (!hasCapture(index))
        return -1;
    return m_ovector.at(index * 2);
}

int RegexEngine::end(int index) const
{
    if (!hasCapture(index))
        return -1;
    return m_ovector.at(index * 2 + 1);
}

QString RegexEngine::captureText(const char *subject, int index) const
{
    const int s = start(index);
    const int e = end(index);
    if (!subject || s < 0 || e < s)
        return QString();
    return QString::fromUtf8(subject + s, e - s);
}
