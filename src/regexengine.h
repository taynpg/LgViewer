#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

// PCRE2 (8-bit) 的轻量封装
// 只暴露日志查看器需要的能力：编译 + 单行匹配 + 命名捕获组取值
class RegexEngine
{
public:
    RegexEngine();
    ~RegexEngine();

    RegexEngine(const RegexEngine &) = delete;
    RegexEngine &operator=(const RegexEngine &) = delete;

    // 编译；pattern 为空或非法时 isValid()==false，error/errorOffset 带回原因
    bool compile(const QString &pattern, QString *error = nullptr, int *errorOffset = nullptr);
    void release();

    bool isValid() const { return m_code != nullptr; }

    // 1 = 命中, 0 = 未命中, 负数 = PCRE2 错误码
    int match(const char *subject, int length);
    // 从 startOffset 开始匹配（用于同一行内找多处命中）
    int matchAt(const char *subject, int length, int startOffset);

    int     captureCount() const { return m_captureCount; }
    int     groupIndex(const QString &name) const;
    QStringList groupNames() const;

    bool hasCapture(int index) const;
    int  start(int index) const; // 相对 subject 的字节偏移，未参与匹配返回 -1
    int  end(int index) const;
    QString captureText(const char *subject, int index) const;

    QString pattern() const { return m_pattern; }
    QString lastError() const { return m_lastError; }

    // 1 = JIT 生效, 0 = 未启用, -1 = 编译 JIT 失败
    int jitState() const { return m_jit; }

private:
    void freeAll();

    QString m_pattern;
    QString m_lastError;
    void   *m_code = nullptr;
    void   *m_matchData = nullptr;
    void   *m_matchContext = nullptr;
    void   *m_jitStack = nullptr;
    int     m_jit = 0;
    int     m_captureCount = 0;
    QVector<int> m_ovector;
    QStringList  m_names;
    QVector<int> m_nameNumbers;
};
