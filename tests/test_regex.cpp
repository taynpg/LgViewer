// RegexEngine 自检：JIT、命名捕获组、逐行匹配
#include "regexengine.h"

#include <cstdio>
#include <QCoreApplication>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    const char *line1 = "[2026-05-29 08:44:46.210][info]: 创建订单 [ORD-20260529-0012]，金额 [128.50] 元@//order_service.cpp/:OrderService::Submit:96,4012";
    const char *line2 = "14:28:12.588 [Info] scheduler tick, pending tasks = 3";

    struct Case { const char *pattern; const char *subject; };
    const Case cases[] = {
        { "^\\[(?<time>[^\\]]+)\\]\\s*\\[(?<level>[^\\]]+)\\]\\s*:?\\s*(?<msg>.*?)@(?<loc>\\S+?)(?:$|,(?<tid>\\d+)$)", line1 },
        { "^(?<time>\\d{2}:\\d{2}:\\d{2}[.,]\\d{3})\\s+\\[(?<level>[^\\]]+)\\]\\s*(?<msg>.*)$", line2 },
    };

    int failed = 0;
    for (const Case &c : cases) {
        RegexEngine eng;
        QString err;
        if (!eng.compile(QString::fromUtf8(c.pattern), &err)) {
            std::printf("FAIL compile: %s\n", qPrintable(err));
            ++failed;
            continue;
        }
        std::printf("jit=%d names=[", eng.jitState());
        for (const QString &n : eng.groupNames())
            std::printf("%s ", qPrintable(n));
        std::printf("]\n");

        const QByteArray subject = QString::fromUtf8(c.subject).toUtf8();
        if (eng.match(subject.constData(), subject.size()) != 1) {
            std::printf("FAIL no match\n");
            ++failed;
            continue;
        }
        for (const QString &n : eng.groupNames()) {
            const int gi = eng.groupIndex(n);
            std::printf("  %-6s = %s\n", qPrintable(n),
                        qPrintable(eng.captureText(subject.constData(), gi)));
        }
    }
    std::printf(failed ? "RESULT: FAIL\n" : "RESULT: OK\n");
    return failed;
}
