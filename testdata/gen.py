"""生成 LgViewer 的演示 / 压力测试日志。

内容全部为虚构的业务场景（订单、支付、缓存、任务调度），与任何真实系统无关。
用法：python gen.py
输出：sample_a.log / sample_b.log / big.log / gbk_sample.txt
"""

import datetime
import os
import random

out = os.path.dirname(os.path.abspath(__file__))
os.makedirs(out, exist_ok=True)

# ---------------------------------------------------------------- 格式 A
# [时间][等级]: 消息@//文件/:函数:行号,线程号
levels_a = ["info", "debug", "info", "info", "warn", "info", "error", "debug", "info", "fatal"]
files_a = ["//order_service.cpp/", "//payment_client.cpp/", "//cache_store.cpp/"]
funcs_a = ["OrderService::Submit", "PaymentClient::Send", "CacheStore::Refresh"]

msgs_a = [
    "创建订单 [ORD-20260529-0012]，金额 [128.50] 元",
    "订单 [ORD-20260529-0012] 已写入数据库，耗时 6 ms",
    "支付网关响应：[SUCCESS,0000]，流水号 [PAY2026052900001]",
    "调用库存服务超时，准备第 2 次重试",
    "库存扣减失败：SKU [A-10023] 可用量不足",
    "本地缓存已刷新，命中率 98.7%",
    "加载配置项 42 个，来源 config/app.toml",
    "消息队列积压 1204 条，已触发扩容",
    "工作线程池扩容至 16 个线程",
    "磁盘剩余空间不足 5%，写入可能失败",
]

t = datetime.datetime(2026, 5, 29, 8, 44, 46, 210000)
lines_a = []
for i in range(200):
    lv = levels_a[i % len(levels_a)]
    msg = msgs_a[i % len(msgs_a)]
    f = files_a[i % len(files_a)]
    fn = funcs_a[i % len(funcs_a)]
    ts = t.strftime("%Y-%m-%d %H:%M:%S.") + "%03d" % (t.microsecond // 1000)
    lines_a.append(f"[{ts}][{lv}]: {msg}@{f}:{fn}:{60 + i},4012")
    t += datetime.timedelta(milliseconds=random.randint(3, 900))

with open(os.path.join(out, "sample_a.log"), "w", encoding="utf-8", newline="\n") as fh:
    fh.write("\n".join(lines_a) + "\n")

# ---------------------------------------------------------------- 格式 B
msgs_b = [
    "scheduler tick, pending tasks = 3",
    "worker[2] start job JOB-2041",
    "worker[2] finish job JOB-2041 in 18 ms",
    "cache warmup done, 1204 keys loaded",
    "got SIGUSR1, reload config",
    "job JOB-2042 waiting for free worker",
    "queue length = 0, idle workers = 6",
    "retry job JOB-2043, attempt 2/3",
    "connect to upstream 192.0.2.12:9000 timeout",
    "health check ok, uptime 3d 04:15",
]
levels_b = ["Info", "Info", "Info", "Info", "Info", "Debug", "Info", "Info", "Error", "Info"]

t2 = datetime.datetime(2026, 5, 29, 14, 28, 12, 588000)
lines_b = []
for i in range(500):
    ts = t2.strftime("%H:%M:%S.") + "%03d" % (t2.microsecond // 1000)
    lines_b.append(f"{ts} [{levels_b[i % len(levels_b)]}] {msgs_b[i % len(msgs_b)]}")
    t2 += datetime.timedelta(milliseconds=random.randint(1, 40))

with open(os.path.join(out, "sample_b.log"), "w", encoding="utf-8", newline="\n") as fh:
    fh.write("\n".join(lines_b) + "\n")

# ------------------------------------------------- 大文件（约 100 万行）
big_path = os.path.join(out, "big.log")
t3 = datetime.datetime(2026, 5, 29, 0, 0, 0, 0)
with open(big_path, "w", encoding="utf-8", newline="\n") as fh:
    for i in range(1000000):
        lv = levels_a[i % len(levels_a)]
        msg = msgs_a[i % len(msgs_a)]
        f = files_a[i % len(files_a)]
        fn = funcs_a[i % len(funcs_a)]
        ts = t3.strftime("%Y-%m-%d %H:%M:%S.") + "%03d" % (t3.microsecond // 1000)
        fh.write(f"[{ts}][{lv}]: {msg}@{f}:{fn}:{60 + (i % 900)},4012\n")
        t3 += datetime.timedelta(milliseconds=17)

# ---------------------------------------------------------------- GBK 编码
gbk_lines = [
    "[2026-05-29 09:00:00.001][info]: 服务启动完成，监听端口 8080",
    "[2026-05-29 09:00:00.052][warn]: 上游响应较慢：耗时 1832 ms",
    "[2026-05-29 09:00:01.113][error]: 请求失败，错误码 0x8007，请检查网络",
    "[2026-05-29 09:00:01.880][info]: 重新连接成功，耗时 767 ms",
    "[2026-05-29 09:00:02.001][debug]: 缓存命中率 98.7%，队列长度 12",
]
with open(os.path.join(out, "gbk_sample.txt"), "w", encoding="gbk", newline="\n") as fh:
    fh.write("\n".join(gbk_lines * 40) + "\n")

print("big.log size:", round(os.path.getsize(big_path) / 1024 / 1024, 1), "MB")
print("done")
