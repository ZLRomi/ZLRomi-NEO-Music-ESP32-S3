#include "pinyin.h"

#include <string.h>

#include "gfx.h"
#include "theme.h"

/* 内嵌常用音节 -> 汉字表，覆盖中文歌名与歌手名里的高频字。
   词条按拼音排序，匹配时先精确、再前缀。表可继续扩充，不影响逻辑。 */
typedef struct {
    const char *py;
    const char *han;
} py_entry_t;

static const py_entry_t PY_TABLE[] = {
    { "a",    "啊阿爱" },        { "ai",   "爱哎矮碍" },
    { "an",   "安按暗岸" },      { "ang",  "昂" },
    { "ao",   "奥傲熬" },        { "ba",   "吧把八爸罢" },
    { "bai",  "白百摆败" },      { "ban",  "半班办般" },
    { "bang", "帮棒旁" },        { "bao",  "包保抱宝" },
    { "bei",  "北被备悲" },      { "ben",  "本笨" },
    { "bi",   "比笔必彼" },      { "bian", "边变便遍" },
    { "biao", "表标" },          { "bie",  "别" },
    { "bing", "并病冰兵" },      { "bo",   "波播薄" },
    { "bu",   "不部步布" },      { "cai",  "才菜彩猜" },
    { "can",  "参残惨" },        { "cao",  "草曹" },
    { "ce",   "测侧" },          { "cha",  "查茶差" },
    { "chang","长唱常场" },      { "chen", "陈沉晨" },
    { "cheng","成城程称" },      { "chi",  "吃迟尺" },
    { "chong","重冲充" },        { "chu",  "出处初楚" },
    { "chuan","传船穿" },        { "ci",   "次词此" },
    { "cong", "从丛" },          { "da",   "大达打答" },
    { "dai",  "带待代" },        { "dan",  "但单淡" },
    { "dang", "当党挡" },        { "dao",  "到道倒岛" },
    { "de",   "的得" },          { "deng", "等灯登" },
    { "di",   "地第低底" },      { "dian", "点电典" },
    { "dong", "东动懂冬" },      { "dou",  "都斗豆" },
    { "du",   "度读独" },        { "duan", "段短断" },
    { "dui",  "对队" },          { "duo",  "多朵躲" },
    { "er",   "而二耳" },        { "fa",   "发法" },
    { "fan",  "反饭凡烦" },      { "fang", "方放房" },
    { "fei",  "飞非费" },        { "fen",  "分份纷" },
    { "feng", "风封疯逢" },      { "fu",   "父福服复" },
    { "gai",  "该改" },          { "gan",  "干感敢" },
    { "gang", "刚钢" },          { "gao",  "高告" },
    { "ge",   "个歌格哥" },      { "gei",  "给" },
    { "gen",  "跟根" },          { "geng", "更" },
    { "gong", "工公共" },        { "gou",  "够狗" },
    { "gu",   "古故鼓" },        { "guan", "关观管" },
    { "guang","光广" },          { "gui",  "归贵鬼" },
    { "guo",  "国过果" },        { "hai",  "还海孩" },
    { "han",  "汉寒喊" },        { "hang", "行航" },
    { "hao",  "好号豪" },        { "he",   "和何合河" },
    { "hei",  "黑" },            { "hen",  "很恨" },
    { "heng", "横恒" },          { "hong", "红洪虹" },
    { "hou",  "后候厚" },        { "hu",   "忽湖虎护" },
    { "hua",  "花话画华" },      { "huai", "坏怀" },
    { "huan", "换还欢环" },      { "huang","黄荒皇" },
    { "hui",  "回会灰悔" },      { "huo",  "火或活" },
    { "ji",   "几记计季寄" },    { "jia",  "家加假架" },
    { "jian", "见间件简" },      { "jiang","将江讲" },
    { "jiao", "叫教角脚" },      { "jie",  "解接姐街" },
    { "jin",  "进近今金" },      { "jing", "京静经晴" },
    { "jiu",  "就九旧酒" },      { "ju",   "句举据" },
    { "jue",  "觉绝决" },        { "jun",  "军君" },
    { "kai",  "开" },            { "kan",  "看" },
    { "kao",  "靠考" },          { "ke",   "可克客刻" },
    { "ken",  "肯" },            { "kong", "空控" },
    { "kou",  "口" },            { "ku",   "苦哭" },
    { "kuai", "快块" },          { "kun",  "困" },
    { "la",   "啦拉辣" },        { "lai",  "来" },
    { "lan",  "蓝懒烂" },        { "lang", "浪郎" },
    { "lao",  "老劳" },          { "le",   "了乐" },
    { "lei",  "类泪累" },        { "leng", "冷" },
    { "li",   "里理力李" },      { "lian", "连脸恋" },
    { "liang","两亮量" },        { "liao", "了聊料" },
    { "lin",  "林临" },          { "ling", "零灵另" },
    { "liu",  "六流留" },        { "long", "龙笼" },
    { "lou",  "楼漏" },          { "lu",   "路陆露" },
    { "lv",   "绿旅" },          { "luan", "乱" },
    { "luo",  "落罗" },          { "ma",   "吗妈马" },
    { "mai",  "买麦卖" },        { "man",  "满慢漫" },
    { "mang", "忙盲" },          { "mao",  "毛猫冒" },
    { "me",   "么" },            { "mei",  "没每美妹" },
    { "men",  "们门" },          { "meng", "梦蒙" },
    { "mi",   "米密迷" },        { "mian", "面免眠" },
    { "miao", "秒妙描" },        { "min",  "民" },
    { "ming", "名明命" },        { "mo",   "莫末默" },
    { "mu",   "木母目" },        { "na",   "那拿哪" },
    { "nai",  "乃奶" },          { "nan",  "南难男" },
    { "nao",  "闹脑" },          { "ne",   "呢" },
    { "nei",  "内" },            { "neng", "能" },
    { "ni",   "你泥逆" },        { "nian", "年念" },
    { "niang","娘" },            { "niao", "鸟" },
    { "ning", "宁" },            { "niu",  "牛" },
    { "nong", "农弄" },          { "nu",   "努怒" },
    { "nv",   "女" },            { "nuan", "暖" },
    { "pai",  "排派拍" },        { "pan",  "盘盼" },
    { "pang", "旁胖" },          { "pao",  "跑抛" },
    { "pei",  "陪配" },          { "peng", "朋碰" },
    { "pi",   "皮疲" },          { "pian", "片便骗" },
    { "piao", "飘票" },          { "pin",  "品拼" },
    { "ping", "平评瓶" },        { "po",   "破坡颇" },
    { "pu",   "普铺" },          { "qi",   "起气七奇" },
    { "qian", "前钱千浅" },      { "qiang","强墙抢" },
    { "qiao", "桥巧敲" },        { "qie",  "切且" },
    { "qin",  "亲琴勤" },        { "qing", "青清情请" },
    { "qiu",  "秋求球" },        { "qu",   "去取曲" },
    { "quan", "全权圈" },        { "que",  "却确缺" },
    { "qun",  "群" },            { "ran",  "然燃" },
    { "rang", "让" },            { "rao",  "绕" },
    { "re",   "热" },            { "ren",  "人认任" },
    { "reng", "仍" },            { "ri",   "日" },
    { "rong", "容荣" },          { "rou",  "肉柔" },
    { "ru",   "如入" },          { "ruan", "软" },
    { "rui",  "瑞" },            { "san",  "三散" },
    { "se",   "色" },            { "sha",  "沙傻" },
    { "shan", "山善闪" },        { "shang","上商伤" },
    { "shao", "少烧" },          { "she",  "舍设社" },
    { "shei", "谁" },            { "shen", "深身神" },
    { "sheng","生声胜" },        { "shi",  "是时世事十" },
    { "shou", "手收受首" },      { "shu",  "书数树" },
    { "shuai","帅摔" },          { "shuang","双爽" },
    { "shui", "水睡" },          { "shuo", "说" },
    { "si",   "四死思" },        { "song", "送松宋" },
    { "su",   "速诉素" },        { "suan", "算酸" },
    { "sui",  "岁随碎" },        { "sun",  "孙" },
    { "suo",  "所锁" },          { "ta",   "他她它踏" },
    { "tai",  "太台抬" },        { "tan",  "谈弹坛" },
    { "tang", "糖汤躺" },        { "tao",  "逃桃讨" },
    { "te",   "特" },            { "teng", "疼腾" },
    { "ti",   "题提体" },        { "tian", "天甜填" },
    { "tiao", "条跳挑" },        { "tie",  "铁贴" },
    { "ting", "听停厅" },        { "tong", "同通痛童" },
    { "tou",  "头偷投" },        { "tu",   "图突土" },
    { "tuan", "团" },            { "tui",  "推退" },
    { "tuo",  "脱拖妥" },        { "wai",  "外歪" },
    { "wan",  "完晚万玩" },      { "wang", "忘望王往" },
    { "wei",  "为位味未围" },    { "wen",  "问文闻温" },
    { "weng", "翁" },            { "wo",   "我握窝" },
    { "wu",   "无五午舞雾" },    { "xi",   "西希洗细喜" },
    { "xia",  "下夏吓" },        { "xian", "先现线限" },
    { "xiang","想向乡相香" },    { "xiao", "小笑晓消" },
    { "xie",  "写谢些鞋" },      { "xin",  "心新信辛" },
    { "xing", "星行醒姓" },      { "xiong","兄熊" },
    { "xiu",  "修休秀" },        { "xu",   "需许须续" },
    { "xuan", "选悬" },          { "xue",  "学雪血" },
    { "xun",  "寻询" },          { "ya",   "呀压牙" },
    { "yan",  "眼烟演言" },      { "yang", "样阳养洋" },
    { "yao",  "要摇药遥" },      { "ye",   "也夜叶爷" },
    { "yi",   "一以已衣意" },    { "yin",  "因音银饮" },
    { "ying", "应影英赢" },      { "yong", "用永勇" },
    { "you",  "有又游友右" },    { "yu",   "雨与鱼语遇" },
    { "yuan", "远圆愿园" },      { "yue",  "月约越乐" },
    { "yun",  "云运" },          { "za",   "杂" },
    { "zai",  "在再载" },        { "zan",  "咱赞" },
    { "zao",  "早造糟" },        { "ze",   "责择" },
    { "zen",  "怎" },            { "zeng", "增曾" },
    { "zha",  "炸眨" },          { "zhan", "站占战" },
    { "zhang","张章长涨" },      { "zhao", "找照招" },
    { "zhe",  "这着者" },        { "zhen", "真阵珍" },
    { "zheng","正整争" },        { "zhi",  "只知直纸" },
    { "zhong","中种重众" },      { "zhou", "周州舟" },
    { "zhu",  "住主注助" },      { "zhuan","转专" },
    { "zhuang","装壮状" },       { "zhui", "追" },
    { "zhun", "准" },            { "zi",   "子自字紫" },
    { "zong", "总纵" },          { "zou",  "走奏" },
    { "zu",   "组足祖" },        { "zuan", "钻" },
    { "zui",  "最醉嘴" },        { "zuo",  "做左坐座" },
};

#define PY_ENTRIES  (sizeof(PY_TABLE) / sizeof(PY_TABLE[0]))

/* 10x4 键格：前三行是字母 + 功能，第四行前 8 格是候选，后 2 格是操作 */
static const char *KEY_LABEL[PY_KEYS] = {
    "q","w","e","r","t","y","u","i","o","p",
    "a","s","d","f","g","h","j","k","l","退格",
    "z","x","c","v","b","n","m","清空","空格","确定",
    "", "", "", "", "", "", "", "", "搜索", "清除",
};

/* ASCII 布局：给 WiFi 密码这类纯英数字输入用，第 4 行是数字与符号 */
static const char *KEY_LABEL_ASCII[PY_KEYS] = {
    "q","w","e","r","t","y","u","i","o","p",
    "a","s","d","f","g","h","j","k","l","退格",
    "z","x","c","v","b","n","m","1","2","3",
    "4","5","6","7","8","9","0","-","确定","清除",
};

static kb_mode_t s_mode = KB_PINYIN;

void pinyin_set_mode(kb_mode_t mode)
{
    s_mode = mode;
}

void pinyin_reset(pinyin_t *p)
{
    if (!p) return;
    memset(p, 0, sizeof(*p));
    pinyin_update_candidates(p);
}

void pinyin_update_candidates(pinyin_t *p)
{
    if (!p) return;
    memset(p->cands, 0, sizeof(p->cands));
    p->cand_count = 0;
    if (p->input[0] == '\0') return;

    /* 先精确匹配，把该音节的所有字都列出来 */
    for (size_t i = 0; i < PY_ENTRIES; i++) {
        if (strcmp(PY_TABLE[i].py, p->input) != 0) continue;
        const char *h = PY_TABLE[i].han;
        while (*h && p->cand_count < PY_CAND_MAX) {
            size_t n = (*h & 0x80) ? 3 : 1;
            if (n > 3) n = 3;
            memcpy(p->cands[p->cand_count], h, n);
            p->cands[p->cand_count][n] = '\0';
            p->cand_count++;
            h += n;
        }
        return;
    }

    /* 无精确匹配时按前缀给候选，方便边拼边选 */
    size_t len = strlen(p->input);
    for (size_t i = 0; i < PY_ENTRIES && p->cand_count < PY_CAND_MAX; i++) {
        if (strncmp(PY_TABLE[i].py, p->input, len) != 0) continue;
        const char *h = PY_TABLE[i].han;
        size_t n = (*h & 0x80) ? 3 : 1;
        memcpy(p->cands[p->cand_count], h, n);
        p->cands[p->cand_count][n] = '\0';
        p->cand_count++;
    }
}

pinyin_key_t pinyin_key_type(int index)
{
    if (index < 0 || index >= PY_KEYS) return PK_NONE;
    if (s_mode == KB_ASCII) {
        if (index == 19) return PK_BACK;
        if (index == 38) return PK_OK;
        if (index == 39) return PK_CLEAR_ALL;
        return PK_CHAR;
    }
    /* 字母：0..9 是 q..p，10..18 是 a..l，20..26 是 z..m */
    if (index <= 18 || (index >= 20 && index <= 26)) return PK_LETTER;
    if (index == 19) return PK_BACK;
    if (index == 27) return PK_CLEAR_ALL;
    if (index == 28) return PK_SPACE;
    if (index == 29) return PK_SEARCH;         /* 确定 */
    if (index <= 37) return PK_CANDIDATE;
    if (index == 38) return PK_SEARCH;
    return PK_CLEAR_INPUT;                     /* 39 清除 */
}

void pinyin_key_label(int index, const pinyin_t *p, char *buf, size_t cap)
{
    if (!buf || cap == 0) return;
    buf[0] = '\0';
    if (index < 0 || index >= PY_KEYS) return;

    pinyin_key_t t = pinyin_key_type(index);
    if (s_mode == KB_ASCII) {
        strncpy(buf, KEY_LABEL_ASCII[index], cap - 1);
        buf[cap - 1] = '\0';
        return;
    }
    if (t == PK_CANDIDATE) {
        int ci = index - 30;
        if (p && ci < p->cand_count && p->cands[ci][0]) {
            strncpy(buf, p->cands[ci], cap - 1);
            buf[cap - 1] = '\0';
        } else {
            strncpy(buf, "-", cap - 1);
            buf[cap - 1] = '\0';
        }
        return;
    }
    strncpy(buf, KEY_LABEL[index], cap - 1);
    buf[cap - 1] = '\0';
}

static void append_utf8(char *dst, size_t cap, const char *src)
{
    size_t dl = strlen(dst), sl = strlen(src);
    if (dl + sl + 1 > cap) return;
    memcpy(dst + dl, src, sl + 1);
}

static void backspace_utf8(char *s)
{
    size_t n = strlen(s);
    if (n == 0) return;
    size_t start = n - 1;
    while (start > 0 && (s[start] & 0xC0) == 0x80) start--;
    s[start] = '\0';
}

static void commit_candidate(pinyin_t *p, int ci)
{
    if (!p || ci < 0 || ci >= p->cand_count) return;
    append_utf8(p->text, sizeof(p->text), p->cands[ci]);
    p->input[0] = '\0';
    pinyin_update_candidates(p);
}

void pinyin_activate(pinyin_t *p, int index)
{
    if (!p) return;
    switch (pinyin_key_type(index)) {
    case PK_LETTER: {
        size_t n = strlen(p->input);
        if (n + 2 <= sizeof(p->input)) {
            p->input[n] = KEY_LABEL[index][0];
            p->input[n + 1] = '\0';
            pinyin_update_candidates(p);
        }
        break;
    }
    case PK_BACK:
        if (p->input[0]) {
            p->input[strlen(p->input) - 1] = '\0';
            pinyin_update_candidates(p);
        } else {
            backspace_utf8(p->text);
        }
        break;
    case PK_CLEAR_ALL:
        p->input[0] = '\0';
        p->text[0] = '\0';
        pinyin_update_candidates(p);
        break;
    case PK_CLEAR_INPUT:
        p->input[0] = '\0';
        pinyin_update_candidates(p);
        break;
    case PK_SPACE:
        append_utf8(p->text, sizeof(p->text), " ");
        break;
    case PK_CANDIDATE:
        commit_candidate(p, index - 30);
        break;
    case PK_CHAR: {
        /* ASCII 模式：按键上的字符直接追加到已输入文本 */
        char label[16];
        pinyin_key_label(index, p, label, sizeof(label));
        if (label[0]) append_utf8(p->text, sizeof(p->text), label);
        break;
    }
    case PK_OK:
        p->submit = true;
        break;
    case PK_SEARCH:
        /* 还有拼音没上屏时，先把首选候选补上，避免搜到拼音串 */
        if (p->input[0] && p->cand_count > 0) commit_candidate(p, 0);
        if (p->text[0]) p->submit = true;
        break;
    default:
        break;
    }
}

const char *pinyin_text(const pinyin_t *p)
{
    return p ? p->text : "";
}

/* 键盘格子尺寸：10 列 x 4 行，正好铺满 320 宽的屏幕 */
#define KB_CELL_W   30
#define KB_CELL_H   42
#define KB_CELL_GAP 1

void pinyin_draw_keyboard(const pinyin_t *p, int focus_index, int x0, int y0)
{
    for (int i = 0; i < PY_KEYS; i++) {
        int col = i % PY_COLS;
        int row = i / PY_COLS;
        int x = x0 + col * (KB_CELL_W + KB_CELL_GAP);
        int y = y0 + row * (KB_CELL_H + KB_CELL_GAP);
        bool focused = (i == focus_index);

        uint16_t bg = focused ? g_theme.focus : g_theme.surface;
        if (pinyin_key_type(i) == PK_CANDIDATE && p->cand_count > 0) {
            bg = focused ? g_theme.focus : g_theme.surface_hi;
        }
        gfx_rect(x, y, x + KB_CELL_W, y + KB_CELL_H, bg);
        if (focused) {
            gfx_frame(x, y, x + KB_CELL_W, y + KB_CELL_H, g_theme.focus_edge);
        }

        char label[16];
        pinyin_key_label(i, p, label, sizeof(label));
        if (label[0]) {
            int tw = gfx_text_width_small(label);
            int tx = x + (KB_CELL_W - tw) / 2;
            if (tx < x) tx = x;
            gfx_text_small(tx, y + (KB_CELL_H - 16) / 2, label,
                           focused ? g_theme.accent : g_theme.text, bg);
        }
    }
}
