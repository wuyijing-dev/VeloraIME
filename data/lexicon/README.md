# 基础词库来源

构建将基础数据作为 Qt 资源。用户可以把同名替换文件放到环境变量 `VELORA_LEXICON_DIR` 指定的目录，重启应用后读取。发布的静态联网词包继续走单独的签名验证与安装流程。

| 文件 | 上游与版本 | 许可 |
| --- | --- | --- |
| `luna.dict.yaml` | 朙月拼音 `luna_pinyin.dict.yaml`，提交 `84685a7e7793e43278ca29368977b79d74d37a03` | [LGPL-3.0](LICENSE.Luna)，字典文件保留上游归属说明 |
| `essay.txt` | Rime 八股文，提交 `d95eeae056df429ecf993038c325a216f0b28c5b` | [LGPL-3.0](LICENSE.Essay) |
| `TSCharacters.txt`、`TSPhrases.txt` | OpenCC 1.1.9，提交 `556ed22496d650bd0b13b6c163be9814637970ae` | [Apache-2.0](LICENSE.OpenCC) |
| `english.txt` | CMU Pronouncing Dictionary，提交 `74790861f652b15e4ac49015a90074ad62a27690` | [CMU 许可](LICENSE.CMU) |

前三项保留原始文件内容。英文词表移除音素、重复读音和非 ASCII 条目。可用以下命令从对应版本的 `cmudict.dict` 重新生成。

```sh
LC_ALL=C awk '$1 ~ /^[a-z]+$/ {print $1}' cmudict.dict | LC_ALL=C sort -u > english.txt
```

中文引擎读取 Luna 的显式拼音，使用 Essay 提供词频，并为部分发音无歧义的高频词生成音节组合。简体输出使用 OpenCC 映射，繁体输出保留源词，双拼采用小鹤双拼编码。美式和英式英文使用同一基础表，英式常用变体由本项目的排序表补充。

[朙月拼音](https://github.com/rime/rime-luna-pinyin) · [Rime 八股文](https://github.com/rime/rime-essay) · [OpenCC](https://github.com/BYVoid/OpenCC) · [CMUdict](https://github.com/cmusphinx/cmudict) · [小鹤双拼](https://flypy.cc/)
