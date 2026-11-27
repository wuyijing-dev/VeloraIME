.pragma library

function action(value, label, weight, icon, accessibleLabel) {
    return {
        "action": value, "label": label, "weight": weight || 1,
        "icon": icon || "", "accessibleLabel": accessibleLabel || label
    }
}

function letters(chinese, mode) {
    const top = keys("qwertyuiop", "1234567890")
    const middle = keys("asdfghjkl")
    const bottom = keys("zxcvbnm")
    bottom.unshift(action("shift", "", 1.36, "shift"))
    bottom.push(action("backspace", "", 1.36, "backspace"))
    const extra = mode === "email" ? textKey("@") : mode === "url" ? textKey("/") : textKey(chinese ? "、" : ",")
    const period = textKey(chinese ? "。" : ".")
    return [top, middle, bottom, footer(chinese, extra, period, false)]
}

function symbols(extended, chinese, locale) {
    const top = keys(extended ? "[]{}#%^*+=" : "1234567890")
    const sterling = locale === "en-GB"
    const currencies = sterling ? "_\\|~<>€$¥" : "_\\|~<>€£¥"
    const middle = keys(extended ? currencies : (sterling ? "@#£&*()-+" : "@#$&*()-+"))
    const bottom = keys(chinese ? "，。！？：；" : ",.!?:;")
    bottom.unshift(action(extended ? "symbol" : "extended", extended ? "123" : "符号", 1.5))
    bottom.push(action("backspace", "", 1.5, "backspace"))
    const extra = textKey("/")
    const period = textKey(chinese ? "。" : ".")
    return [top, middle, bottom, footer(chinese, extra, period, true)]
}

function numbers(mode) {
    const isPhone = mode === "phone"
    const decimal = mode === "decimal"
    const bottom = [textKey(isPhone ? "*" : (decimal ? "." : "")), textKey("0"),
                   textKey(isPhone ? "#" : (decimal ? "-" : "")),
                   action("enter", "", 1.2, "enter")]
    return [
        [textKey("1"), textKey("2"), textKey("3"), action("backspace", "", 1.2, "backspace")],
        [textKey("4"), textKey("5"), textKey("6"), isPhone ? textKey("+") : action("left", "", 1.2, "left")],
        [textKey("7"), textKey("8"), textKey("9"), isPhone ? textKey(",") : action("right", "", 1.2, "right")],
        bottom
    ]
}

function nine() {
    return [
        [action("symbol", "123"), t9("2", "ABC"), t9("3", "DEF"), action("backspace", "", 0.91, "backspace")],
        [t9("4", "GHI"), t9("5", "JKL"), t9("6", "MNO"), textKey("，", 0.91)],
        [t9("7", "PQRS"), t9("8", "TUV"), t9("9", "WXYZ"), textKey("。", 0.91)],
        [action("language", "中 / EN"), action("space", "空格", 2), action("enter", "", 0.91, "enter")]
    ]
}

function keys(value, details) {
    return Array.from(value, (character, index) => {
        const key = textKey(character)
        key.detail = details ? details[index] : ""
        return key
    })
}

function textKey(text, weight) {
    return { "action": "text", "label": text, "weight": weight || 1 }
}

function t9(digit, group) {
    return { "action": "text", "label": digit, "detail": group, "weight": 1,
             "accessibleLabel": digit + " " + group, "nine": true }
}

function footer(chinese, extra, period, symbolsVisible) {
    return [action(symbolsVisible ? "letters" : "symbol", symbolsVisible ? (chinese ? "拼音" : "ABC") : "123", 1.15),
            action("language", "中 / EN", 1.38), extra,
            action("space", chinese ? "空格" : "Space", 4.65),
            period, action("enter", "", 1.45, "enter")]
}

const accents = {
    "a": "àáâãäåæā", "c": "çćč", "e": "èéêëēėę", "i": "ìíîïī",
    "n": "ñń", "o": "òóôõöøœō", "s": "ßśš", "u": "ùúûüū", "y": "ÿý",
    "z": "źžż", ",": "，、；：", ".": "。…！？", "!": "¡", "?": "¿",
    "-": "–—−_", "$": "¥€£₩", "£": "€$¥₩", "@": "＠", "/": "／\\",
    "，": ",、；：", "。": ".…！？", "、": "，,；："
}

function alternatives(key, allowAccents, shifted) {
    if (key.action !== "text" || key.nine)
        return []
    const lower = key.label.toLowerCase()
    const variants = accents[lower] ?? ""
    const value = allowAccents || !/[a-z]/.test(lower) ? variants : ""
    return Array.from((key.detail ?? "") + (shifted ? value.toUpperCase() : value))
}

const emoji = [
    ["😀", "grin happy smile 开心 微笑", "faces"], ["😄", "laugh smile 笑脸", "faces"],
    ["😂", "tears joy 笑哭", "faces"], ["🙂", "smile 微笑", "faces"],
    ["😉", "wink 眨眼", "faces"], ["🥰", "love hearts 喜爱", "faces"],
    ["🤔", "think 思考", "faces"], ["😎", "cool glasses 墨镜", "faces"],
    ["😢", "cry sad 哭泣", "faces"], ["🥳", "party celebrate 派对", "faces"],
    ["😴", "sleep 困觉", "faces"], ["😡", "angry 生气", "faces"],
    ["👍", "yes like thumbs up 赞", "people"], ["👏", "clap 鼓掌", "people"],
    ["🙏", "thanks pray 谢谢", "people"], ["👋", "wave hello 挥手", "people"],
    ["🤝", "handshake 握手", "people"], ["👀", "eyes 眼睛", "people"],
    ["❤️", "red heart love 爱心", "people"], ["💙", "blue heart 蓝心", "people"],
    ["🌟", "star 星星", "things"], ["✨", "sparkles 闪光", "things"],
    ["🎉", "party celebrate 庆祝", "things"], ["🎂", "birthday cake 蛋糕", "things"],
    ["🚀", "rocket 火箭", "things"], ["⚡", "lightning 闪电", "things"],
    ["☀️", "sun 太阳", "things"], ["🌙", "moon 月亮", "things"],
    ["🍎", "apple 苹果", "things"], ["☕", "coffee 咖啡", "things"],
    ["🐱", "cat 猫", "things"], ["🐶", "dog 狗", "things"],
    ["🏠", "home house 房子", "things"], ["🎵", "music 音乐", "things"]
]

function emojiKeys(category, query, recent) {
    if (query !== "")
        return emoji.filter(entry => entry[1].includes(query.toLowerCase())).map(entry => entry[0])
    if (category === "recent")
        return recent
    return emoji.filter(entry => entry[2] === category).map(entry => entry[0])
}
