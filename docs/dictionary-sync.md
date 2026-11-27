# 跨设备加密词库快照

VeloraIME 可以把用户主动导出的词库 JSON 交换到另一台设备。每次同步由用户发起，设备在私有本地目录读写明文。共享目录、同步盘或 U 盘只保存加密快照。脚本需要 Linux、Python 3.10+ 和命令行工具 `age`。工具也可在能够读取导出文件的主机上运行。 [age 项目](https://github.com/FiloSottile/age) · [age 文件格式](https://age-encryption.org/v1)

## 配置设备

在每台设备的本地私有目录运行 `age-keygen -o velora-key.txt`。在设备间交换 `age1…` 公钥，私钥留在各设备。发布时列出全部需要读取该组词库的设备公钥，包括当前发布设备。选择共享目录后设置相同的 `--profile` 标签和不同的 `--device` 标签。标签仅使用小写字母、数字、连字符和下划线。

从 VeloraIME 设置中的个人词库工具导出 `export.json` 到私有目录。以下命令示范设备 A 发布首个快照。示例环境变量由用户设置为实际路径和公钥。

```sh
python3 tools/sync_user_dictionary.py \
  --shared "$VELORA_SHARED" --profile personal --identity "$VELORA_IDENTITY_A" \
  push --device tablet --input "$VELORA_PRIVATE_A/export.json" \
  --recipient "$VELORA_PUBLIC_A" --recipient "$VELORA_PUBLIC_B"
```

在设备 B 把最新的唯一快照解密到新的本地文件，并用个人词库工具导入。

```sh
python3 tools/sync_user_dictionary.py \
  --shared "$VELORA_SHARED" --profile personal --identity "$VELORA_IDENTITY_B" \
  pull --output "$VELORA_PRIVATE_B/received.json"
```

输出文件仅允许当前用户读写，已有路径会报错。私钥、原始导出和解密文件需要留在共享目录之外。应用导入采用合并，保留较高使用次数、较新的使用时间和已有用户词标记。删除词条需要在对应设备上执行，本地学习清除操作也需要逐设备执行。

## 版本与并发冲突

每次发布生成一个完整的不可变快照。文件名是密文的 SHA-256，设备标签、词库内容、时间和父版本 ID 均在加密内容内。共享介质可见档案组名、快照数、文件大小和文件时间。目录复制中断时，读者会看到缺失父版本或无效密文。

- 运行 `list` 查看 `heads`、`missingParents` 与快照历史。下一次发布时通过 `--parent <完整快照 ID>` 指明当前版本。
- 如果两台设备分别发布同一父版本，会产生多个分支。可用 `pull --snapshot <完整快照 ID>` 分别取回，将需要的词条在本地合并并重新导出。发布合并后快照时为每个分支头指定一次 `--parent`。
- 已见当前分支与传入父版本不一致时，脚本拒绝发布。先同步目录并查阅历史，随后决定合并内容。
- 新设备只持有自己的密钥时，可按 ID 读取已经为它加密的快照。完整历史依赖相应时期的公钥名单。更换全部参与密钥时，可在已授权设备导出需要传承的内容，使用新标签建立新档案组。

任何知道接收者公钥的人都能生成可解密文件，快照目录应只允许受信设备写入。传输介质可删除或隐藏文件，重要快照需单独备份。此工具会保留原来的不可变密文历史，清理历史前应先决定需要保留的恢复点。

## 验证

本机有 `age` 和 `age-keygen` 时，执行 `python3 -m unittest discover -s tests -p test_sync_user_dictionary.py`。测试用临时密钥和目录验证加密往返、错误密钥和损坏文件拒绝、并发分支识别及显式合并。
