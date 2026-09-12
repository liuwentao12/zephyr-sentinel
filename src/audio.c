#include "audio.h"
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2s.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/printk.h>



#define AUDIO_BLOCK_SIZE 512
#define AUDIO_BLOCK_COUNT  4

static const struct device *i2s_dev;

/*
 * I2S RX 驱动使用的固定内存池。
 * DMA / I2S 驱动先把数据放到这些 block 中。
 */
K_MEM_SLAB_DEFINE_STATIC(rx_mem_slab, AUDIO_BLOCK_SIZE, AUDIO_BLOCK_COUNT, 4);

static struct i2s_config i2s_cfg =
{
    .word_size = 16,
    .channels = 2,
    .format = I2S_FMT_DATA_FORMAT_I2S,
    /* MAX9867 提供 BCLK/LRCLK，MCU 接收外部时钟。 */
    .options =I2S_OPT_FRAME_CLK_TARGET | I2S_OPT_BIT_CLK_TARGET,
    .frame_clk_freq = 16000,
    .block_size = AUDIO_BLOCK_SIZE,
    .mem_slab = &rx_mem_slab,
    .timeout = 1000,
};

static int codec_configure(const struct i2c_dt_spec *codec)
{
    int ret;
    /*配置期间关闭音频功能*/
    ret = i2c_reg_write_byte_dt(codec,0x17,0x00);
    /*MCU复位不一定复位codec,先清除上次配置*/
    for (uint8_t reg = 0x04;reg <= 0x16;reg++)
    {
        ret = i2c_reg_write_byte_dt(codec, reg, 0x00);
        if(ret != 0)
        {
            printk("Codec reset reg 0x%02x failed: %d\n",reg, ret);
            return ret;
        }
    }
    static const uint8_t config[][2] = {
        {0x05, 0x10}, /* MCLK 不分频：PCLK = 12.288 MHz */
        {0x06, 0x20}, /* NI = 0x2000：采样率 16 kHz */
        {0x07, 0x00},
        {0x09, 0x02},  /* BCLK = 48 × LRCLK = 768 kHz */
        {0x08, 0x98}, /* codec 主模式，I2S 格式 */
        {0x0A, 0xA2}, /* 沿用 Zephyr 示例的滤波配置 */
        {0x15, 0x10}, /* 沿用板载数字麦克风输入配置 */
        {0x0D, 0x33}, /* ADC 数字增益 0 dB */
        {0x0E, 0x4C}, /* 静音 line-in 到播放输出的通路 */
        {0x0F, 0x4C},
        {0x17, 0x83}, /* 退出关断，启用左右 ADC，DAC 保持关闭 */
    };
    for(size_t i =0; i < sizeof(config)/sizeof(config[0]); i++)
    {
        ret = i2c_reg_write_byte_dt(codec, config[i][0], config[i][1]);
        if(ret != 0)
        {
            printk("Codec reg 0x%02x failed: %d\n",config[i][0],ret);
            return ret;
        }
    }
    printk("Max9867 configured :16000Hz\n");
    return 0;
}

int audio_init(void)
{
    const struct i2c_dt_spec codec = I2C_DT_SPEC_GET(DT_NODELABEL(max9867));
    if (!i2c_is_ready_dt(&codec))
    {
        printk("Codec I2c bus not ready\n");
        return -ENODEV;
    }
    uint8_t revision = 0;
    int probe_ret = i2c_reg_read_byte_dt(&codec, 0xFF, &revision);
    if(probe_ret != 0)
    {
        printk("Max9867 read fail: %d\n",probe_ret);
        return probe_ret;
    }
    printk("MAX9867 revision: 0x%02x\n", revision);
    int codec_ret = codec_configure(&codec);
    if (codec_ret != 0)
    {
        printk("Max9867 configure failed: %d\n",codec_ret);
        return codec_ret;
    }

    i2s_dev = DEVICE_DT_GET(DT_NODELABEL(i2s0));

    if(!device_is_ready(i2s_dev))
    {
        printk("I2S device not ready\n");
        return -ENODEV;
    }

    int ret;

    ret = i2s_configure(i2s_dev,I2S_DIR_RX,&i2s_cfg);

    if(ret)
    {
        printk("I2S configure failed: %d\n",ret);
        return ret;
    }
    printk("Audio init OK\n");
    return 0;
}


int audio_start(void)
{
    int ret;
    ret = i2s_trigger(i2s_dev,I2S_DIR_RX,I2S_TRIGGER_START);
    if(ret)
    {
        printk("I2S start failed: %d\n", ret);

        return ret;
    }
    printk("I2S started\n");
    return 0;
}


int audio_read(int16_t *buffer, size_t buffer_size, size_t *read_size)
{
    void *mem_block;
    size_t size;

    int ret = i2s_read(i2s_dev, &mem_block, &size);
    if (ret != 0)
    {
        return ret;
    }
    if (size > buffer_size)
    {
        k_mem_slab_free(&rx_mem_slab, mem_block);
        return -EOVERFLOW;
    }
    memcpy(buffer, mem_block, size);
    k_mem_slab_free(&rx_mem_slab, mem_block);
    *read_size = size;

    return 0;
}