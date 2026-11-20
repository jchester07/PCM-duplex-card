// SPDX-License-Identifier: GPL-2.0
/*
 * PCM1808 ADC codec driver for a chip hardware-strapped as I2S clock MASTER.
 *
 * Strapping assumed: MD1=0, MD0=1 (master, 512fs), FMT=0 (I2S, 24-bit).
 * SCKI = 24.576 MHz  ->  fs = 48 kHz, BCK = 64fs, LRCK = fs.
 *
 * The PCM1808 has no control interface, so this driver only describes the
 * DAI to ASoC: capture only, fixed 48 kHz, codec is clock provider.
 */
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <sound/pcm.h>
#include <sound/soc.h>

static int pcm1808m_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	dev_info(dai->dev, "set_fmt: 0x%08x (fmt=%u provider=0x%x inv=0x%x)\n",
		 fmt, fmt & SND_SOC_DAIFMT_FORMAT_MASK,
		 fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK,
		 fmt & SND_SOC_DAIFMT_INV_MASK);

	/* The chip is strapped for I2S in hardware; anything else is wrong. */
	if ((fmt & SND_SOC_DAIFMT_FORMAT_MASK) != SND_SOC_DAIFMT_I2S)
		return -EINVAL;

	/*
	 * Master/slave and polarity are fixed by the MD0/MD1/FMT pins, so
	 * the driver cannot act on them. Warn instead of failing, because the
	 * core may hand a multi-codec link a flipped provider flag.
	 */
	if ((fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) != SND_SOC_DAIFMT_CBP_CFP)
		dev_warn(dai->dev, "codec is hardware clock provider; ignoring requested provider flags\n");
	if ((fmt & SND_SOC_DAIFMT_INV_MASK) != SND_SOC_DAIFMT_NB_NF)
		dev_warn(dai->dev, "polarity is fixed in hardware; ignoring requested inversion\n");
	return 0;
}

static const struct snd_soc_dai_ops pcm1808m_dai_ops = {
	.set_fmt = pcm1808m_set_fmt,
};

static struct snd_soc_dai_driver pcm1808m_dai = {
	.name = "pcm1808m-hifi",
	.capture = {
		.stream_name  = "Capture",
		.channels_min = 2,
		.channels_max = 2,
		.rates        = SNDRV_PCM_RATE_48000,
		.formats      = SNDRV_PCM_FMTBIT_S32_LE | SNDRV_PCM_FMTBIT_S24_LE,
	},
	.ops = &pcm1808m_dai_ops,
};

static const struct snd_soc_dapm_widget pcm1808m_widgets[] = {
	SND_SOC_DAPM_INPUT("VINL"),
	SND_SOC_DAPM_INPUT("VINR"),
};

static const struct snd_soc_dapm_route pcm1808m_routes[] = {
	{ "Capture", NULL, "VINL" },
	{ "Capture", NULL, "VINR" },
};

static const struct snd_soc_component_driver pcm1808m_component = {
	.dapm_widgets		= pcm1808m_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(pcm1808m_widgets),
	.dapm_routes		= pcm1808m_routes,
	.num_dapm_routes	= ARRAY_SIZE(pcm1808m_routes),
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
};

static int pcm1808m_probe(struct platform_device *pdev)
{
	return devm_snd_soc_register_component(&pdev->dev, &pcm1808m_component,
					       &pcm1808m_dai, 1);
}

static const struct of_device_id pcm1808m_of_match[] = {
	{ .compatible = "custom,pcm1808-master" },
	{ }
};
MODULE_DEVICE_TABLE(of, pcm1808m_of_match);

static struct platform_driver pcm1808m_driver = {
	.driver = {
		.name = "pcm1808-master",
		.of_match_table = pcm1808m_of_match,
	},
	.probe = pcm1808m_probe,
};
module_platform_driver(pcm1808m_driver);

MODULE_DESCRIPTION("PCM1808 ADC codec driver (I2S master, fixed 48 kHz)");
MODULE_LICENSE("GPL");
