/********************************** (C) COPYRIGHT  *******************************
* File Name          : config.c
* Author             : P1nkDog
* Version            : V1.0.0
* Date               : 2026/07/19
* Description        : 手柄配置文件核心逻辑
*********************************************************************************/

#include "config.h"
#include "config_hal.h"
#include "config_crc.h"
#include "ch32v30x.h"
#include <string.h>


//--------------------------------------------------------------------+
// 编译期地址边界校验
//   - A 必须远高于代码段（预留至少 64KB 固件空间）
//   - B 的扇区尾不得越过 256KB 物理 Flash 上界 0x08040000
//   - 两个扇区都要按 4KB 对齐（FLASH_ROM_ERASE/WRITE 的硬性要求）
//--------------------------------------------------------------------+
_Static_assert(CONFIG_FLASH_SECTOR_A_ADDR >= 0x08010000U,
               "配置扇区 A 地址过低，可能与代码段冲突");
_Static_assert(CONFIG_FLASH_SECTOR_B_ADDR + CONFIG_FLASH_SECTOR_SIZE <= 0x08040000U,
               "配置扇区 B 的尾部超出 256KB 物理 Flash 边界");
_Static_assert((CONFIG_FLASH_SECTOR_A_ADDR % CONFIG_FLASH_SECTOR_SIZE) == 0,
               "配置扇区 A 未按 4KB 扇区对齐");
_Static_assert((CONFIG_FLASH_SECTOR_B_ADDR % CONFIG_FLASH_SECTOR_SIZE) == 0,
               "配置扇区 B 未按 4KB 扇区对齐");

//结构体布局固化：2 + 2 + 1 + 1 = 6 字节，且不允许编译器插入隐式填充。
//  该结构体会被整体 memcpy 进 Flash 并与扇区逐字节比对，长度/填充一旦漂移，
//  既会让旧记录被误读，也会让"内容未变"的保存失去跳过擦写的依据。
//  第 6 字节自本版本起由 _reserved 改为 debounce_ms，长度刻意保持不变。
_Static_assert(sizeof(GamepadSettings) == 6,
               "GamepadSettings 布局漂移（应为 6 字节：deadzone_left/right + invert_mask + debounce_ms）");

//按键映射表长度固化：索引直接就是物理位号 0~25，必须是 26 字节且无填充
_Static_assert(sizeof(ButtonMapConfig) == BUTTON_MAP_COUNT,
               "ButtonMapConfig 布局漂移（应为 26 字节）");


//--------------------------------------------------------------------+
// 配置组实例
//--------------------------------------------------------------------+
JoystickCalibConfig g_joystickCalib;
GamepadSettings g_gamepadSettings;
PollingRateConfig g_pollingrate;
ButtonMapConfig g_buttonMap;

//--------------------------------------------------------------------+
// 静态缓冲区
//   sector_buf 直接作为 FLASH_ROM_WRITE() 的源缓冲区（不再经中间拷贝），
//   所以必须 4 字节对齐；同时扇区大小也必须是 4 的倍数，否则末尾会残留
//   不足一个字的尾部、且校验范围与写入长度会对不上。
//--------------------------------------------------------------------+
_Static_assert((CONFIG_FLASH_SECTOR_SIZE % 4U) == 0U, "sector size must be 4-byte aligned");
static uint8_t sector_buf[CONFIG_FLASH_SECTOR_SIZE] __attribute__((aligned(4)));

//--------------------------------------------------------------------+
// 默认值
//--------------------------------------------------------------------+
static const JoystickCalibConfig joystickCalibDefault = {
    .calib = {
        [0] = { .min = 0, .center = 2048, .max = 4095},
        [1] = { .min = 0, .center = 2048, .max = 4095},
        [2] = { .min = 0, .center = 2048, .max = 4095},
        [3] = { .min = 0, .center = 2048, .max = 4095},
    }
};

static const GamepadSettings gamepadSettingsDefault = {
    .deadzone_left  = 40,
    .deadzone_right = 40,
    .invert_mask    = 0,
    .debounce_ms    = 4,
};

static const PollingRateConfig pollingRateDefault = {
    .level = POLL_RATE_1000,
};

//出厂按键映射：与 TASK-02 之前 gamepad.c 里写死的映射逐项等价。
//  下标即物理位号（见 input.h）：0=UP 1=RIGHT 2=DOWN 3=LEFT
//  4=X(□) 5=A(×) 6=B(○) 7=Y(△) 8=L1 9=R1 10=L2 11=R2
//  12=L3 13=R3 14=PS 15=SELECT 16=START 17=TOUCHPAD；18~25（BTN1~8）默认禁用。
//  注意 4~7 的位序（X/A/B/Y）与 DS4 逻辑枚举（A/B/Y/X）不是同序，逐项对齐过。
const ButtonMapConfig buttonMapDefault = {
    .map = {
        [0]  = DS4_BTN_UP,        // PE9  网络UP
        [1]  = DS4_BTN_RIGHT,     // PE8  网络RIGHT
        [2]  = DS4_BTN_DOWN,      // PE11 网络DOWN
        [3]  = DS4_BTN_LEFT,      // PE10 网络LEFT
        [4]  = DS4_BTN_X,         // PB11 网络X  → □ Square
        [5]  = DS4_BTN_A,         // PE13 网络A  → × Cross
        [6]  = DS4_BTN_B,         // PE14 网络B  → ○ Circle
        [7]  = DS4_BTN_Y,         // PE15 网络Y  → △ Triangle
        [8]  = DS4_BTN_L1,        // PE7  L1（高电平有效）
        [9]  = DS4_BTN_R1,        // PB10 R1（高电平有效）
        [10] = DS4_BTN_L2,        // PB0  L2
        [11] = DS4_BTN_R2,        // PB1  R2
        [12] = DS4_BTN_L3,        // PA7  左摇杆按下
        [13] = DS4_BTN_R3,        // PE1  右摇杆按下
        [14] = DS4_BTN_PS,        // PE12 PS
        [15] = DS4_BTN_SELECT,    // PB12 SW2（Share）
        [16] = DS4_BTN_START,     // PB14 SW4（Options）
        [17] = DS4_BTN_TOUCHPAD,  // PB13 SW3
        [18] = DS4_BTN_NONE,      // PD1  BTN1（未映射）
        [19] = DS4_BTN_NONE,      // PD2  BTN2
        [20] = DS4_BTN_NONE,      // PD3  BTN3
        [21] = DS4_BTN_NONE,      // PD4  BTN4
        [22] = DS4_BTN_NONE,      // PD5  BTN5
        [23] = DS4_BTN_NONE,      // PD6  BTN6
        [24] = DS4_BTN_NONE,      // PD7  BTN7
        [25] = DS4_BTN_NONE,      // PB3  BTN8
    }
};

//--------------------------------------------------------------------+
// 回报率间隔映射
//--------------------------------------------------------------------+

static const uint32_t polling_interval_us[] = {
    [POLL_RATE_250]  = 4000,   // 250Hz  → 4000μs
    [POLL_RATE_500]  = 2000,   // 500Hz  → 2000μs
    [POLL_RATE_1000] = 1000,   // 1000Hz → 1000μs
    [POLL_RATE_2000] = 500,    // 2000Hz → 500μs
    [POLL_RATE_4000] = 250,    // 4000Hz → 250μs
    [POLL_RATE_8000] = 125,    // 8000Hz → 125μs
};

uint32_t config_get_poll_interval_us(void) {
    const PollingRateConfig *cfg =
        (const PollingRateConfig *)config_get(CONFIG_ID_POLLING_RATE);
    if (cfg == NULL) return polling_interval_us[POLL_RATE_1000];

    const uint32_t cnt = sizeof(polling_interval_us) / sizeof(polling_interval_us[0]);
    if ((uint32_t)cfg->level >= cnt) return polling_interval_us[POLL_RATE_1000];

    return polling_interval_us[cfg->level];
}

//--------------------------------------------------------------------+
// 配置组校验回调
//   共同约定：data 可能指向 MSP 接收缓冲区或 Flash 扇区缓冲（均为非对齐地址），
//   所以一律先 memcpy 到局部的对齐结构体再读成员，绝不直接强转访问
//   uint16_t 成员，规避 RISC-V 非对齐加载异常。
//--------------------------------------------------------------------+

//摇杆校准：每轴必须满足 min < center < max 且 max <= 4095（12bit ADC 满量程）
//  这条约束是 joystick_map_axis() 的前提：
//    half_neg = center - min > 0、half_pos = max - center > 0，
//    denom = half - dz 才会是正数，不会除零、也不会出现负数映射。
static bool validate_joystick_calib(const void *data){
    if (data == NULL) return false;

    JoystickCalibConfig c;
    memcpy(&c, data, sizeof(c));

    for (uint8_t i = 0; i < 4; i++) {
        if (c.calib[i].min    >= c.calib[i].center) return false;
        if (c.calib[i].center >= c.calib[i].max)    return false;
        if (c.calib[i].max    >  4095U)             return false;
    }
    return true;
}

//手柄设置：左右死区计数不得超限、反转掩码不得含非法位、消抖时间不得超上限
static bool validate_gamepad_settings(const void *data){
    if (data == NULL) return false;

    GamepadSettings s;
    memcpy(&s, data, sizeof(s));

    return (s.deadzone_left  <= CONFIG_DEADZONE_MAX) &&
           (s.deadzone_right <= CONFIG_DEADZONE_MAX) &&
           ((s.invert_mask & ~JOYSTICK_INVERT_MASK_ALL) == 0U) &&
           (s.debounce_ms    <= CONFIG_DEBOUNCE_MAX_MS);
}

//回报率：档位必须落在枚举尾部之内，否则 bInterval 计算会越界
static bool validate_polling_rate(const void *data){
    if (data == NULL) return false;

    PollingRateConfig p;
    memcpy(&p, data, sizeof(p));

    return (p.level <= (uint8_t)POLL_RATE_8000);
}

//按键映射：每一项必须是合法的 Ds4LogicalButton（0=禁用，1~18=功能）
//  越界项会让 gamepad.c 的 (1UL << target) 移位越界（target > 31 是未定义行为），
//  因此写入前必须逐项拒绝，而不是运行期再钳位。
static bool validate_button_map(const void *data){
    if (data == NULL) return false;

    ButtonMapConfig m;
    memcpy(&m, data, sizeof(m));

    for (uint8_t i = 0; i < BUTTON_MAP_COUNT; i++) {
        if (m.map[i] > (uint8_t)DS4_BTN_MAX) return false;
    }
    return true;
}

//--------------------------------------------------------------------+
// 注册表
//--------------------------------------------------------------------+
static struct {
    uint8_t count; // 已注册数量
    ConfigDesc descs[CONFIG_MAX_GROUPS]; // 描述符数组
    bool dirty[CONFIG_MAX_GROUPS]; // 脏标记（true = 需要保存）
} registry;


// 注册一个配置组
// id: 配置组编号
// version: 版本号（修改结构体时递增）
// size: 数据大小
// ram: RAM 中的变量地址
// def: 默认值地址
// name: 名称（调试用）
// validate: 数值合法性校验回调（NULL = 不做范围校验）
static void config_register(uint16_t id, uint8_t version, uint16_t size,
                            void *ram, const void *def, const char *name,
                            ConfigValidator validate){
    if (registry.count >= CONFIG_MAX_GROUPS) return;

    ConfigDesc *d = &registry.descs[registry.count];
    d->id = id;
    d->version = version;
    d->size = size;
    d->ram = ram;
    d->default_data = def;
    d->name = name;
    d->validate = validate;
    registry.dirty[registry.count] = false;

    memcpy(ram, def, size);

    registry.count++;
}

//--------------------------------------------------------------------+
// 查找
//--------------------------------------------------------------------+

static ConfigDesc* find_desc(uint16_t id){
    for(int i = 0; i < registry.count; i++)
    {  
        if (registry.descs[i].id == id) return &registry.descs[i];
    }
    return NULL;
}

static int find_index(uint16_t id){
    for (int i = 0; i < registry.count; i++) {
        if(registry.descs[i].id == id) return i;
    }
    return -1;
}


//--------------------------------------------------------------------+
// api接口
//--------------------------------------------------------------------+

//重置某个配置为默认值
bool config_reset(uint16_t id){
    ConfigDesc *d = find_desc(id);
    if(!d) return false;
    memcpy(d->ram, d->default_data, d->size);
    registry.dirty[find_index(id)] = true;
    return true;
}

//重置所有配置为默认值
void config_reset_all(void){
    for (int i = 0; i < registry.count; i++) {
        memcpy(registry.descs[i].ram,
               registry.descs[i].default_data,
               registry.descs[i].size);
        registry.dirty[i] = true;
    }
}

//获得配置组的只读指针
const void* config_get(uint16_t id){
    ConfigDesc *d = find_desc(id);
    return  d ? d->ram : NULL;
}

//获得配置组的可写指针
void* config_get_mutable(uint16_t id){
    ConfigDesc *d = find_desc(id);
    return  d ? d->ram : NULL;
}

//标记某个配置组已修改
void config_mark_dirty(uint16_t id){
    int idx = find_index(id);
    if (idx >= 0) registry.dirty[idx] = true;
}

//校验某个配置组的数值是否合法
//  data 允许是 MSP 接收缓冲区这类非对齐地址，具体回调内部已做局部拷贝。
bool config_validate(uint16_t id, const void *data){
    if (data == NULL) return false;

    ConfigDesc *d = find_desc(id);
    if (d == NULL) return false;

    // 未挂校验回调 = 该组不做数值范围校验
    if (d->validate == NULL) return true;

    return d->validate(data);
}

//获取已经注册的配置组
uint8_t config_get_count(void){
    return registry.count;
}

//获取配置组描述符
const ConfigDesc* config_get_desc(uint16_t id){
    return find_desc(id);
}

const ConfigDesc* config_get_desc_by_index(uint8_t index){
    return (index < registry.count) ? &registry.descs[index] : NULL;
}


//--------------------------------------------------------------------+
// flash加载
//--------------------------------------------------------------------+

//验证一个扇区的数据是否有效
//  旧实现把整扇区读进全局 sector_buf 再算 CRC，等于"用待写入的数据源当作校验读回
//  缓冲"——正确性完全依赖调用顺序（config_save 里必须"先写后验"），任何人调整顺序
//  都会静默写坏数据。现在改成：头部/尾部各读一个小块，中间按 64B 分块流式累加 CRC，
//  校验过程不碰任何共享缓冲区。
static bool validate_sector(uint32_t addr){
    ConfigHeader h;
    config_hal_read(addr, (uint8_t *)&h, sizeof(h));
    if (h.magic != CONFIG_MAGIC) return false;

    uint8_t tail[2];
    config_hal_read(addr + CONFIG_FLASH_SECTOR_SIZE - 2, tail, sizeof(tail));
    uint16_t stored = (uint16_t)(tail[0] | ((uint16_t)tail[1] << 8));

    uint16_t crc = 0xFFFFu;
    uint8_t chunk[64];
    const uint32_t total = CONFIG_FLASH_SECTOR_SIZE - 2u;

    for (uint32_t off = 0; off < total; off += sizeof(chunk)) {
        uint32_t n = total - off;
        if (n > sizeof(chunk)) n = sizeof(chunk);
        config_hal_read(addr + off, chunk, n);
        crc = crc16_update(crc, chunk, n);
    }

    return (stored == crc);
}

//从指定扇区加载数据
static void load_sector(uint32_t addr){
    config_hal_read(addr, sector_buf, sizeof(sector_buf));

    ConfigHeader *h = (ConfigHeader *)sector_buf;
    if (h->magic != CONFIG_MAGIC) return;

    //格式版本不匹配直接放弃：宁可让 RAM 保留出厂默认值，也不要把旧布局的字节
    //按新结构体解释（那会产出"结构正确、语义荒谬"的配置，比恢复默认更难排查）。
    if (h->version != CONFIG_HEADER_VERSION) return;

    //遍历所有tlv数据
    uint32_t offset = sizeof(ConfigHeader);
    while(offset + sizeof(ConfigRecord) <= CONFIG_FLASH_SECTOR_SIZE - 4)
    {
        ConfigRecord *rec = (ConfigRecord *)(sector_buf + offset);

        //遇到终止符(0x0000)停止
        if (rec->type == 0x0000) break;

        //记录长度异常，停止
        if (rec->length < sizeof(ConfigRecord) || offset + rec->length > CONFIG_FLASH_SECTOR_SIZE - 4) break;

        //在配置表中查找匹配的配置组
        ConfigDesc *d = find_desc(rec->type);
        if (d && rec->version == d->version){
            uint16_t data_len = rec->length - sizeof(ConfigRecord);

            //两道门槛才允许覆盖 RAM：
            //  1) 记录长度必须够（旧代码用 min() 做部分拷贝，会把结构体填成
            //     "前半新、后半旧"的混合态，这里改为长度不足直接不认这条记录）；
            //  2) 内容必须通过该组的合法性校验（防止 Flash 被写坏 / 老固件留下的
            //     越界值把运行期打挂）。
            // 任一不满足则跳过，RAM 保留 config_register() 灌入的出厂默认值。
            if (data_len >= d->size && config_validate(d->id, rec->data)) {
                memcpy(d->ram, rec->data, d->size);
            }
        }
        offset += rec->length;
    }
}


//--------------------------------------------------------------------+
// flash保存
//--------------------------------------------------------------------+

bool config_save(void){
    //检查是否有脏数据
    bool has_dirty = false;
    for (int i = 0; i < registry.count; i++) {
        if (registry.dirty[i]){
            has_dirty = true;
            break;
        }
    }
    if(!has_dirty) return true;

    
    //在ram中构建完整的tlv数据
    memset(sector_buf, 0xFF, sizeof(sector_buf));
    uint32_t offset = 0;

    //写header
    ConfigHeader header = {
        .magic = CONFIG_MAGIC,
        .version = CONFIG_HEADER_VERSION,
        .count = registry.count,
    };
    memcpy(sector_buf + offset, &header, sizeof(header));
    offset += sizeof(header);

    //写每个配置组的record
    for (int i = 0; i < registry.count; i++) {
        ConfigDesc *d = &registry.descs[i];

        //落盘前的最后一道防线：任何组只要当前 RAM 内容不合法，整次保存直接失败。
        //  存在的风险路径：某组正处于"改了一半"的中间态，而 config_save() 是遍历
        //  写全部组，只要别的组变脏就会把它一并串行化。没有这道守卫，非法内容会
        //  带着合法 CRC 进入 Flash；下次启动虽然会被 load_sector() 的校验挡下来，
        //  但代价是该组被重置为出厂默认值（用户数据丢失，且现场表现为"莫名恢复默认"）。
        //  改为"拒绝保存"后：RAM 与 Flash 都保持原样，上位机收到
        //  MSP_STATUS_FLASH_ERROR，用户可先恢复合法状态再重发 CONFIG_SAVE。
        if (d->validate != NULL && !d->validate(d->ram)) {
            return false;
        }

        ConfigRecord rec = {
            .type = d->id,
            .length = sizeof(ConfigRecord) + d->size,
            .version = d->version,
            .flags = 0,
        };

        //空间检查：放不下就整体失败返回，绝不静默 break。
        //  静默截断会让 Flash 里的配置比 RAM 少几组，而调用方仍以为保存成功。
        //  -4 是给 footer(2B) 和末尾 CRC(2B) 预留的空间。
        if (offset + rec.length > CONFIG_FLASH_SECTOR_SIZE - 4) {
            return false;
        }

        memcpy(sector_buf + offset, &rec, sizeof(rec));
        offset += sizeof(rec);
        memcpy(sector_buf + offset, d->ram, d->size);
        offset += d->size;
    }
    
    //写footer（同样要确认放得下，末 2 字节留给 CRC）
    if (offset + sizeof(ConfigFooter) > CONFIG_FLASH_SECTOR_SIZE - 2) {
        return false;
    }
    ConfigFooter footer = {.terminator = 0};
    memcpy(sector_buf + offset, &footer, sizeof(footer));
    offset += sizeof(footer);

    //写crc
    uint16_t crc = crc16_update(0xFFFF, sector_buf, CONFIG_FLASH_SECTOR_SIZE - 2);
    sector_buf[CONFIG_FLASH_SECTOR_SIZE - 2] = (uint8_t)(crc & 0xFF);
    sector_buf[CONFIG_FLASH_SECTOR_SIZE - 1] = (uint8_t)(crc >> 8);

    //--------------------------------------------------------------------+
    // 无实质变化则跳过擦写
    //  Flash 擦写寿命量级 1 万次，而"保存"是上位机的高频动作（拖一次滑块就可能
    //  触发一次 CONFIG_SAVE）。先与主扇区 A 逐字节比对：完全一致就说明这次保存
    //  没有任何内容变化，直接清脏标记返回成功，把寿命留给真正需要落盘的修改。
    //--------------------------------------------------------------------+
    {
        uint8_t chunk[64];
        bool identical = true;

        for (uint32_t off = 0; off < sizeof(sector_buf); off += sizeof(chunk)) {
            config_hal_read(CONFIG_FLASH_SECTOR_A_ADDR + off, chunk, sizeof(chunk));
            if (memcmp(chunk, sector_buf + off, sizeof(chunk)) != 0) {
                identical = false;
                break;
            }
        }

        if (identical) {
            for (int i = 0; i < registry.count; i++) {
                registry.dirty[i] = false;
            }
            return true;
        }
    }

    //【改动】先写暂存扇区 B，校验通过后再覆盖主扇区 A
    //
    //  关中断已下沉到 config_hal 层：擦除是一次硬件操作的原子窗口，写入按
    //  256B 硬件页逐页保护、页与页之间放开中断。业务层不再跨"擦除+整扇区
    //  写入"长时间屏蔽全局中断（旧实现单段屏蔽约 20~30ms）。

    //第1步：写 B（暂存）
    if(!config_hal_erase_sector(CONFIG_FLASH_SECTOR_B_ADDR)){
        return false;
    }
    if(!config_hal_write(CONFIG_FLASH_SECTOR_B_ADDR, sector_buf, sizeof(sector_buf))){
        return false;
    }
    if (!validate_sector(CONFIG_FLASH_SECTOR_B_ADDR)) {
        return false;
    }

    //第2步：用同一份 buffer 覆盖 A（提交，A 永远是主读扇区）
    if(!config_hal_erase_sector(CONFIG_FLASH_SECTOR_A_ADDR)){
        return false;
    }
    if(!config_hal_write(CONFIG_FLASH_SECTOR_A_ADDR, sector_buf, sizeof(sector_buf))){
        return false;
    }
    if (!validate_sector(CONFIG_FLASH_SECTOR_A_ADDR)) {
        return false;
    }

    //保存成功：清除全部脏标记
    for(int i = 0; i < registry.count; i++)
    {
        registry.dirty[i] =false; 
    }

    return true;
}


//--------------------------------------------------------------------+
// 初始化
//--------------------------------------------------------------------+

//出厂默认值自检：逐组用各自的校验回调检验 default_data
//  只在 config_init() 里调一次，成本是 3 次遍历，换取"默认值永不非法"的保证。
static bool config_validate_defaults(void){
    for (uint8_t i = 0; i < registry.count; i++) {
        const ConfigDesc *d = &registry.descs[i];
        if (d->validate != NULL && !d->validate(d->default_data)) {
            return false;
        }
    }
    return true;
}

void config_init(void){
    registry.count = 0;

    // 注册所有配置组
    // 参数：编号, 版本, 大小, RAM地址, 默认值地址, 名称, 校验回调
    config_register(CONFIG_ID_JOYSTICK_CALIB, 1, sizeof(JoystickCalibConfig), 
                    &g_joystickCalib, &joystickCalibDefault, "joystick_calib",
                    validate_joystick_calib);
    // 组版本递增到 4：第 6 字节语义由 _reserved(恒 0) 改为 debounce_ms，
    // 旧 Flash 里 version<=3 的记录会在 load_sector() 的 rec->version == d->version
    // 判断处被整条跳过，RAM 回落到新的出厂默认值（否则会把旧的 0 误读成"关闭消抖"）。
    config_register(CONFIG_ID_GAMEPAD_SETTINGS, 4, sizeof(GamepadSettings), 
                    &g_gamepadSettings, &gamepadSettingsDefault, "gamepad_settings",
                    validate_gamepad_settings);
    config_register(CONFIG_ID_POLLING_RATE, 1, sizeof(PollingRateConfig),
                    &g_pollingrate, &pollingRateDefault, "polling_rate",
                    validate_polling_rate);

    config_register(CONFIG_ID_BUTTON_MAP, 1, sizeof(ButtonMapConfig),
                    &g_buttonMap, &buttonMapDefault, "button_map",
                    validate_button_map);

    // 出厂默认值自检：默认值是所有回退路径的终点，必须 100% 通过自己的校验回调。
    // 不通过说明"校验规则"与"默认常量"不一致，属于开发期缺陷——此时停在死循环里，
    // 比带着非法参数继续跑更容易定位（不引 assert()/abort()，避免把 newlib 的
    // 格式化输出链进来）。
    if (!config_validate_defaults()) {
        for (;;) { __asm volatile ("nop"); }
    }

    //从config加载
    bool a_ok = validate_sector(CONFIG_FLASH_SECTOR_A_ADDR);
    bool b_ok = validate_sector(CONFIG_FLASH_SECTOR_B_ADDR);
   
    if(a_ok){
        load_sector(CONFIG_FLASH_SECTOR_A_ADDR);
    } else if (b_ok) {
        load_sector(CONFIG_FLASH_SECTOR_B_ADDR);
    }
    // 两个扇区都无效时不加载，RAM 保持 config_register() 灌入的默认值
}