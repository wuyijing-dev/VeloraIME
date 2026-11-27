# 云端词库和热词

`LexiconPackManager` 每个实例管理一个版本化数据源。设置 `packId`、`manifestUrl`、`trustedKeys` 后，由应用设置开启 `enabled`。下载请求按日检查版本，按 ETag 减少传输，失败指数退避。设备的候选和个人数据留在本机。两个数据源由宿主组合后绑定给 `KeyboardController.supplementalEntries`。

| 数据源 | `packId` | 发布位置 |
| --- | --- | --- |
| 简体行业扩展词库 | `zh-dictionary` | `/VeloraIME/dictionary/manifest.json` |
| 简体热词 | `zh-hotwords` | `/VeloraIME/hotwords/manifest.json` |

`trustedKeys` 以键 ID 映射 Ed25519 公钥 PEM，例如 `{ "production-1": pinnedPublicPem }`。发布公钥需随应用或可信的宿主配置分发。轮换时先在宿主配置并存新旧公钥，再让签发器用新键 ID。词包与键 ID 都由签名绑定。客户端通过 `importDirectory(fileUrl)` 导入包含 `manifest.json` 及其引用内容文件的目录，联网开关保持独立。管理界面可读取 `activeVersion`、`previousVersion`、`busy` 和 `lastError`，并调用 `rollback()` 恢复上一版。回退保留见过的最高版本，后续网络更新与导入都要求更高版本。

## 签名和发布

`packs/publish.py` 使用 Python 3.11+ 和 OpenSSL 3。清单是单文件 JSON 信封。字段 `signed` 与 `signature` 分别对原始 UTF-8 元数据及 Ed25519 签名编码为 Base64，设备直接对解码后的字节验签。元数据绑定同目录的 `pack-<version>.json`、SHA-256、时间范围、客户端兼容版本和大小。签发脚本只保留词条文本、拼音、权重和可选过期时间。默认简体拼音数据以 NFC UTF-8、无声调 ASCII 拼音表示，允许用半角撇号分隔音节。

构建本地词包需要准备自己的 Ed25519 签名密钥，并使用匹配的公钥配置测试设备。示例命令的 `$PACK_SIGNING_KEY_PATH` 指向项目树之外的私钥文件。将 `--published-at` 设为固定 UTC 时间可以复现生成的内容。测试导入时，设备时间需位于词包有效期内。

```sh
python3 packs/publish.py --entries packs/dictionary.zh-CN.json \
  --key "$PACK_SIGNING_KEY_PATH" --key-id local-1 --pack-id zh-dictionary \
  --kind dictionary --version 1 --valid-days 30 --output /tmp/velora-dictionary
```

GitHub Actions 手动运行 **Publish signed word packs**，从默认分支已审核的 JSON 构建两个数据源。使用仓库 secret `PACK_SIGNING_PRIVATE_KEY` 保存完整的未加密 Ed25519 PKCS#8 PEM，密钥 ID 为 `production-1`，发布源为 GitHub Actions。每次发布使用新的运行编号。签名发布成功后，设备可取 `https://wuyijing-dev.github.io` 下表中的路径。可将同一发布产物用于自有静态镜像或离线介质。 [GitHub Pages 工作流配置](https://docs.github.com/en/pages/getting-started-with-github-pages/using-custom-workflows-with-github-pages)

## 来源与审核

初始行业词和热词由项目人工整理。词表保留编辑来源，现有词条是通用词、公开产品名与技术名称。GitHub Actions 定时调用 Wikimedia Analytics API，生成 **Collect trends for editorial review** 工作流产物。维护者从草稿中核查公开性、词义、多音字和时效，填写拼音、权重和来源后更新审核词表。标题及访问量草稿保留 14 天，签发器只读取审核词表。Wikimedia 页标题和访问量的进一步使用与分发依照其 API 使用规范和对应内容条款。 [Wikimedia 趋势 API](https://doc.wikimedia.org/generated-data-platform/aqs/analytics-api/examples/project-metrics.html#most-viewed-pages) · [Wikimedia API 使用规范](https://foundation.wikimedia.org/wiki/Policy:Wikimedia_Foundation_API_Usage_Guidelines)
