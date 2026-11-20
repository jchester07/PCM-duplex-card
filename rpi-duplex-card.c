// SPDX-License-Identifier: GPL-2.0
/*
 * Machine driver: Raspberry Pi I2S (slave) + PCM5102A DAC + PCM1808 ADC.
 *
 * One DAI link, two codec components. The PCM1808 is the clock provider
 * (BCK/LRCK), the Pi I2S block and the PCM5102A are both consumers.
 * The PCM5102A is playback-only and the PCM1808 capture-only, so ASoC
 * routes each direction to the right chip.
 *
 * Kernel >= 6.10 API names. On older kernels replace snd_soc_rtd_to_cpu()
 * with asoc_rtd_to_cpu() (5.18-6.9).
 */
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#define DUPLEX_RATE	48000	/* 24.576 MHz / 512 */

static struct snd_soc_dai_link_component cpu_comp;
static struct snd_soc_dai_link_component plat_comp;
static struct snd_soc_dai_link_component codec_comps[] = {
	{ .dai_name = "pcm5102a-hifi" },	/* playback */
	{ .dai_name = "pcm1808m-hifi" },	/* capture  */
};

static int duplex_startup(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *rt = substream->runtime;
	int ret;

	/* The ADC owns the clocks and they cannot change: lock the rate. */
	ret = snd_pcm_hw_constraint_single(rt, SNDRV_PCM_HW_PARAM_RATE,
					   DUPLEX_RATE);
	if (ret)
		return ret;
	return snd_pcm_hw_constraint_single(rt, SNDRV_PCM_HW_PARAM_CHANNELS, 2);
}

static int duplex_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);
	int ret;

	/*
	 * The PCM1808 generates BCK/LRCK, so the Pi I2S must be a clock
	 * CONSUMER. Set this explicitly on the CPU DAI (from its own point of
	 * view) instead of trusting the link's flipped dai_fmt: the codec was
	 * seen receiving consumer flags, so the core may have flipped them.
	 */
	ret = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_I2S |
				  SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBC_CFC);
	if (ret && ret != -ENOTSUPP) {
		dev_err(rtd->dev, "cpu set_fmt failed: %d\n", ret);
		return ret;
	}
	dev_info(rtd->dev, "Pi I2S forced to clock consumer, bclk ratio 64\n");

	/*
	 * As a consumer the Pi cannot derive the frame length from its own
	 * divider. The PCM1808 in master mode gives 64 BCK/frame.
	 */
	return snd_soc_dai_set_bclk_ratio(cpu_dai, 64);
}

static const struct snd_soc_ops duplex_ops = {
	.startup   = duplex_startup,
	.hw_params = duplex_hw_params,
};

static struct snd_soc_dai_link duplex_link = {
	.name		= "PCM5102A-PCM1808",
	.stream_name	= "Duplex HiFi",
	/* format is given from the codec's point of view: codec = provider */
	.dai_fmt	= SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
			  SND_SOC_DAIFMT_CBP_CFP,
	.ops		= &duplex_ops,
	.cpus		= &cpu_comp,
	.num_cpus	= 1,
	.codecs		= codec_comps,
	.num_codecs	= ARRAY_SIZE(codec_comps),
	.platforms	= &plat_comp,
	.num_platforms	= 1,
};

static struct snd_soc_card duplex_card = {
	.name		= "PiDuplex",
	.owner		= THIS_MODULE,
	.dai_link	= &duplex_link,
	.num_links	= 1,
};

static int duplex_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device_node *i2s, *dac, *adc;
	int ret;

	i2s = of_parse_phandle(np, "i2s-controller", 0);
	dac = of_parse_phandle(np, "dac-codec", 0);
	adc = of_parse_phandle(np, "adc-codec", 0);
	if (!i2s || !dac || !adc) {
		dev_err(&pdev->dev, "missing i2s-controller/dac-codec/adc-codec\n");
		ret = -EINVAL;
		goto out;
	}

	cpu_comp.dai_name = NULL;
	cpu_comp.of_node = i2s;
	plat_comp.of_node = i2s;
	codec_comps[0].of_node = dac;
	codec_comps[1].of_node = adc;

	duplex_card.dev = &pdev->dev;
	ret = devm_snd_soc_register_card(&pdev->dev, &duplex_card);
	if (ret)
		dev_err_probe(&pdev->dev, ret, "card registration failed\n");
out:
	of_node_put(i2s);
	of_node_put(dac);
	of_node_put(adc);
	return ret;
}

static const struct of_device_id duplex_of_match[] = {
	{ .compatible = "custom,rpi-duplex-card" },
	{ }
};
MODULE_DEVICE_TABLE(of, duplex_of_match);

static struct platform_driver duplex_driver = {
	.driver = {
		.name = "rpi-duplex-card",
		.of_match_table = duplex_of_match,
	},
	.probe = duplex_probe,
};
module_platform_driver(duplex_driver);

MODULE_DESCRIPTION("Raspberry Pi full-duplex card: PCM5102A DAC + PCM1808 ADC");
MODULE_LICENSE("GPL");
