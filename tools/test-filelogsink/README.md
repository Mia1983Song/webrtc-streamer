# FileLogSink standalone test driver

驗證 `FileLogSink` 的 rotation / thread-safety / append 行為，**不需要 WebRTC tree**。
透過 `stubs/` 下的 mock header 取代 `absl::string_view` 與 `webrtc::LogSink`。

跑這個 driver 是 PLAN.md「Verification → 程式層 → Layer 1」對應的工具。

## Build & Run（Windows，需 cmake + MSVC 或 clang）

從 repo root：

```powershell
cmake -B tools/test-filelogsink/build -S tools/test-filelogsink
cmake --build tools/test-filelogsink/build --config Release
.\tools\test-filelogsink\build\Release\test_filelogsink.exe
```

預期輸出：

```
=== FileLogSink standalone tests ===
[PASS] basic write (...)
[PASS] rotation (4 files present, no .4)
[PASS] append across restart (...)
[PASS] multithread (50000 lines, no loss, no interleave)
[PASS] oversized single message (...)

ALL TESTS PASSED
```

exit code 0 = 全過；任一 CHECK fail 即 abort 並印 file:line。

## Build & Run（Linux / macOS）

```bash
cmake -B tools/test-filelogsink/build -S tools/test-filelogsink
cmake --build tools/test-filelogsink/build -j
./tools/test-filelogsink/build/test_filelogsink
```

## Test cases

| # | 驗什麼 | 怎麼驗 |
|---|--------|--------|
| 1 | 基本寫入 | 寫 10 行，確認檔案存在、內容有序 |
| 2 | Rotation | 1 KB cap × 4 files，寫 5 KB → 確認 `.1` `.2` `.3` 生成、`.4` 不生（最舊被刪） |
| 3 | Append 跨 session | 開檔→關→重開同 path → 確認舊內容沒被清掉 |
| 4 | Multi-thread | 5 threads × 10k lines → 確認總行數正確、每 thread 首中尾 marker 都在 |
| 5 | 超大單筆訊息 | 單筆 5 KB > 1 KB cap → 確認不會在空檔上無限 rotate（依 `m_currentBytes > 0` guard） |

## 與正式 binary 的差異

stubs 取代了：
- `absl::string_view` → `std::string_view`（C++17 之後 binary 等效）
- `webrtc::LogSink` 介面 → 2 個 pure virtual（FileLogSink 實際 override 的）

**沒測到的部分**：
- `webrtc::LogMessage::AddLogToStream` 註冊 / 卸載流程（需真 webrtc）
- 實際 webrtc log 格式（時間戳、thread id、severity prefix）
- 跨 capturer thread 的 lock contention（要實際 webrtc 才能重現）

這些靠 PLAN.md Verification Layer 2/3（GitHub Actions 編出的 .exe + 正式機 24h 觀察）涵蓋。

## 不會跑壞 repo 的東西

driver 寫的測試檔都在 `test-out/`（相對於執行當下的 cwd）。建議跑完手動 `rm -rf test-out` 或加進 `.gitignore`（已加）。
