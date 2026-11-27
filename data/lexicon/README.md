# 基础词库来源

构建将基础数据作为 Qt 资源。用户可以把同名替换文件放到环境变量 `VELORA_LEXICON_DIR` 指定的目录，重启应用后读取。发布的静态联网词包继续走单独的签名验证与安装流程。

| 文件 | 上游与版本 | 许可 |
| --- | --- | --- |
| `luna.dict.yaml` | 朙月拼音 `luna_pinyin.dict.yaml`，提交 `84685a7e7793e43278ca29368977b79d74d37a03` | [LGPL-3.0](LICENSE.Luna)，字典文件保留上游归属说明 |
| `essay.txt` | Rime 八股文，提交 `d95eeae056df429ecf993038c325a216f0b28c5b` | [LGPL-3.0](LICENSE.Essay) |
| `TSCharacters.txt`、`TSPhrases.txt` | OpenCC 1.1.9，提交 `556ed22496d650bd0b13b6c163be9814637970ae` | [Apache-2.0](LICENSE.OpenCC) |
| `english.txt` | CMU Pronouncing Dictionary，提交 `74790861f652b15e4ac49015a90074ad62a27690` | [CMU 许可](LICENSE.CMU) |
| `cedict-pronunciations.tsv` | CC-CEDICT 社区与 MDBG，快照 `2026-10-07T03:26:22Z`。源于 Paul Andrew Denisowski 1997、1998 年版权的 CEDICT | [CC BY-SA 4.0](LICENSE.CC-BY-SA-4.0) |
| `english-frequency.tsv` | Robyn Speer，wordfreq 3.0.2，© 2022，DOI `10.5281/zenodo.7199437` | [CC BY-SA 4.0](LICENSE.CC-BY-SA-4.0) |

前三项保留原始文件内容。英文词表移除音素、重复读音和非 ASCII 条目。可用以下命令从对应版本的 `cmudict.dict` 重新生成。

```sh
LC_ALL=C awk '$1 ~ /^[a-z]+$/ {print $1}' cmudict.dict | LC_ALL=C sort -u > english.txt
```

中文引擎结合 Luna 与 CC-CEDICT 的明确释音，使用 Essay 的词频与相邻字对频率做整句分词排序。高频、且发音无歧义的词也可从 Essay 推导音节。简体输出使用 OpenCC 映射，繁体输出保留源词，双拼采用小鹤双拼编码。美式和英式共用基础词表与 wordfreq 词频，本项目附加常用拼写变体。英文纠错考虑 QWERTY 键位距离、重复键和转置。数据全部随包离线使用。

## 重建排序表

使用 Python 3.11 或更新版本和 `msgpack==1.1.2`。从对应版本下载 CC-CEDICT 的 gzip 和 wordfreq 的 wheel，再运行 `python3 data/lexicon/generate-ranking-assets.py --cedict <cedict.gz> --wordfreq <wordfreq.whl>`。脚本校验输入 SHA-256。中文表仅保留二至八字词的简繁形式和无调拼音。英文表保留 ASCII 小写词，以及从 centibel 转换后以百分之一 Zipf 为单位的词频，门槛为 3.00。两个派生数据文件各自保留作者、来源、改动说明和许可证。 [CC-CEDICT 官网](https://cc-cedict.org/wiki/) · [固定 gzip 快照](https://github.com/oxalis-ink/cc-cedict/releases/tag/1.0%2B20261007032622) · [wordfreq 3.0.2](https://pypi.org/project/wordfreq/3.0.2/) · [wordfreq 上游与数据来源](https://github.com/rspeer/wordfreq)

wordfreq 的英文频率估计包含可自由下载的 SUBTLEX-US（Marc Brysbaert、Boris New）和 SUBTLEX-UK（Walter J. B. van Heuven、Paweł Mandera、Emmanuel Keuleers、Marc Brysbaert），以及 OpenSubtitles、Google Books Ngram 等来源。 [SUBTLEX 下载](https://crr.ugent.be/programs-data/subtitle-frequencies) · [OpenSubtitles 语料](https://opus.nlpl.eu/OpenSubtitles.php) · [Google Books Ngram](https://books.google.com/ngrams) · [wordfreq 引用](https://doi.org/10.5281/zenodo.7199437)

## 候选排序回归

[固定语料](../../tests/data/candidate-quality.tsv)覆盖常用词、地名、多音字、整句、全拼、九键、双拼、英文补全与纠错。测试使用空用户配置，关闭学习加权及云词包，期望选项须完整消费输入。运行 `ctest --test-dir build/host -R '^velora\.engine$' -V` 查看统计。数值为工程回归指标，语义存在多种合理答案的输入保留多个候选。

| 固定语料 | 原 Top-1 | 现 Top-1 | 原 Top-3 | 现 Top-3 |
| --- | ---: | ---: | ---: | ---: |
| 中文词 | 18/28 | 28/28 | 20/28 | 28/28 |
| 中文句 | 0/7 | 6/7 | 1/7 | 7/7 |
| 英文补全 | 7/14 | 14/14 | 9/14 | 14/14 |
| 英文纠错 | 8/12 | 11/12 | 9/12 | 12/12 |

[朙月拼音](https://github.com/rime/rime-luna-pinyin) · [Rime 八股文](https://github.com/rime/rime-essay) · [OpenCC](https://github.com/BYVoid/OpenCC) · [CMUdict](https://github.com/cmusphinx/cmudict) · [小鹤双拼](https://flypy.cc/)
