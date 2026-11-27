# 离线识别与词包验证

## 构建验证程序

使用同一个 Qt 6.12 SDK 构建两个可执行文件。语音验证通过 `VoiceRecognizer::decodeFile()` 读取明确指定的本地 WAV，沿用应用的 SenseVoice 解码路径。手写验证使用实际 Zinnia 模型和 QML 形状的笔迹数据。两个程序都无需打开麦克风。

```sh
rk_sdk="$HOME/rk3568"
rk_qt_host="$rk_sdk/qt-host/6.12.0"
rk_qt_target="$rk_sdk/qt-arm64/6.12.0"
export LD_LIBRARY_PATH="$rk_qt_host/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

"$rk_qt_host/bin/qt-cmake" -S tests/recognition -B build/recognition-host
cmake --build build/recognition-host
"$rk_qt_target/bin/qt-cmake" -S tests/recognition -B build/recognition-rk3568 \
  -DQT_CHAINLOAD_TOOLCHAIN_FILE="$rk_sdk/toolchains/rk3568-buildroot.cmake" \
  -DQT_HOST_PATH="$rk_qt_host"
cmake --build build/recognition-rk3568
```

## 主机识别

手写测试需要 Zinnia ABI 0 与 Tegaki 简体中文模型。用 `VELORA_ZINNIA_LIBRARY` 和 `VELORA_HANDWRITING_MODEL` 指向本机安装文件或已经解包的本机发行包。独立测试确认「十」「一」的首选、请求被覆盖、取消结果和缺失模型。 [Zinnia](https://taku910.github.io/zinnia/) · [Tegaki 简体中文模型](https://packages.debian.org/source/sid/tegaki-zinnia-simplified-chinese)

```sh
VELORA_ZINNIA_LIBRARY="$ZINNIA_LIB" VELORA_HANDWRITING_MODEL="$ZINNIA_MODEL" \
  ./build/recognition-host/velora-handwriting-probe
```

语音验证使用上游 SenseVoice 2025-09-09 官方测试音频 `test_wavs/yue-0.wav`。原始文件为 3.072 秒、16 kHz、16 位 PCM 单声道，SHA-256 为 `d029018f0dcaf6bbd66f1f9c1633dc30846e809f4b698a14b70b0849cf77a266`。从上游下载、解包相应机器架构的 sherpa-onnx 运行时，以及 CPU int8 模型包。下面的示例使用已验证的 x64 运行时 v1.12.14。模型和样本按上游分发方式获取。 [SenseVoice 官方测试素材与预期文本](https://k2-fsa.github.io/sherpa/onnx/sense-voice/pretrained.html#sherpa-onnx-sense-voice-zh-en-ja-ko-yue-int8-2025-09-09-chinese-english-japanese-korean-cantonese) · [v1.12.14 运行时](https://github.com/k2-fsa/sherpa-onnx/releases/tag/v1.12.14)

```sh
VELORA_SPEECH_ROOT="$SENSEVOICE_RUNTIME" \
VELORA_SPEECH_MODEL="$SENSEVOICE_MODEL_DIR/model.int8.onnx" \
VELORA_SPEECH_TOKENS="$SENSEVOICE_MODEL_DIR/tokens.txt" \
VELORA_SPEECH_PROVIDER=cpu \
  ./build/recognition-host/velora-voice-fixture-probe \
  "$SENSEVOICE_MODEL_DIR/test_wavs/yue-0.wav" '小企'
```

程序核对真实解码结果、请求的会话令牌、并发请求拦截、启动阶段的同步撤销、运行阶段取消、清理临时音频和拒绝网络路径。应用对焦点变更和敏感字段的第二道检查在 `ctest --test-dir build/host -R 'velora.engine|velora.qml_panel' --output-on-failure` 中验证。

## 板端识别

将 ARM64 验证程序和官方 WAV 样本放入板端临时目录。从板端执行以下命令。默认路径读取板上的 Tegaki 模型与 `/userdata/rk3568-voice` 运行时。其他安装位置可用与主机相同的环境变量覆写。

```sh
export LD_LIBRARY_PATH="/usr/local/qt6.12.0/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
/tmp/velora-qa/velora-handwriting-probe
/tmp/velora-qa/velora-voice-fixture-probe /tmp/velora-qa/yue-0.wav '小企'
```

## 签名词包和断网

`ctest --test-dir build/host -R velora.word_packs --output-on-failure` 在主机启动临时 HTTPS 服务。它用临时发布键签发样包，验证 ETag、关闭联网时零请求、忽略 Cookie、拒绝坏签名/摘要、安装版本回退、高水位、受损缓存时的启动恢复及热词到期后停止暴露候选。

需在板端复测纯数据路径时，先交叉构建 `packs/tests`，在主机用 `python3 packs/tests/run_tests.py --fixtures-only /tmp/velora-pack-fixtures` 生成测试数据。将测试数据与 ARM64 的 `velora_word_pack_tests` 放到板端临时目录。运行下面的场景只读本地测试词包，缓存写入各自的临时目录。短期时效词在主机端的完整 CTest 中验证。

```sh
export VELORA_PACK_TEST_FIXTURES=/tmp/velora-qa/fixtures
/tmp/velora-qa/velora_word_pack_tests validatesImportAndRollback \
  managerExposesVerifiedSnapshot recoversVerifiedPreviousVersionOnRestart \
  preservesSnapshotOnPartialWriteFailure recoversAfterUpdaterTerminatesMidWrite
```

RK3568 上的最后两个用例限制更新进程的写入大小，覆盖有效新版清单落盘后写入失败，以及更新进程在同一路径上终止。两个用例均验证旧版词包和状态字节一致，重启仍能读取，随后可重试。主机装有 `strace` 且允许 `ptrace` 时，完整 CTest 还会在新版清单和正文都暂存成功后，于 `state.json` 的提交写入注入 `ENOSPC` 与 `SIGKILL`。
