#include "stm32f10x_spi.h"
#include "core_cm3.h"
#include "global.h"
#include "cmdSPI.h"

#define MAGIC_BYTE1 0x66
#define MAGIC_BYTE2 0x6F

void spi_cmd_init() {
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_SPI2, ENABLE);

	GPIO_InitTypeDef GPIO_InitStructure;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_13 | GPIO_Pin_15; // PB13-SCK, PB15-MOSI PB14-MISO
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
	GPIO_Init(GPIOB, &GPIO_InitStructure);
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_14;
	GPIO_Init(GPIOB, &GPIO_InitStructure);

	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_2MHz;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_7;
	GPIO_Init(GPIOA, &GPIO_InitStructure); //Torch

	SPI_Cmd(SPI2, DISABLE);
	SPI_InitTypeDef SPI_InitStructure;
	SPI_InitStructure.SPI_Direction = SPI_Direction_2Lines_FullDuplex;
	SPI_InitStructure.SPI_Mode = SPI_Mode_Slave;
	SPI_InitStructure.SPI_DataSize = SPI_DataSize_8b;
	SPI_InitStructure.SPI_CPOL = SPI_CPOL_Low;
	SPI_InitStructure.SPI_CPHA = SPI_CPHA_1Edge;
	SPI_InitStructure.SPI_NSS = SPI_NSS_Soft;
	SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_2;
	SPI_InitStructure.SPI_FirstBit = SPI_FirstBit_MSB;
	SPI_InitStructure.SPI_CRCPolynomial = 7;
	SPI_Init(SPI2, &SPI_InitStructure);
	SPI_CalculateCRC(SPI2, DISABLE);
	SPI_Cmd(SPI2, ENABLE);

	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
	NVIC_InitTypeDef NVIC_InitStructure;
	NVIC_InitStructure.NVIC_IRQChannel = SPI2_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&NVIC_InitStructure);

	USART_DBG_printf("spi2 start interrupt");
	SPI_I2S_ITConfig(SPI2, SPI_I2S_IT_RXNE, ENABLE);
}

static volatile uint8_t spiBuffer[2 + 3], spiBufferPos = 0;
static volatile uint16_t cntUndefined = 0;

void SPI2_IRQHandler(void) {
	if (SPI_I2S_GetITStatus(SPI2, SPI_I2S_IT_RXNE) == SET) {
		uint8_t d = (uint8_t)SPI2->DR;
		SPI_I2S_ClearITPendingBit(SPI2, SPI_I2S_IT_RXNE);
		USART_DBG_printf("spi2:%02x   %d\n\r", d, cntUndefined);
		if ((spiBufferPos == 0 && d == MAGIC_BYTE1) || (spiBufferPos == 1 && d
				== MAGIC_BYTE2) || (spiBufferPos > 1 && spiBufferPos < 5))
			spiBuffer[spiBufferPos++] = d;
		else {
			spiBufferPos = 0;
			cntUndefined++;
			if(cntUndefined > 10) NVIC_SystemReset();
		}

		if (spiBufferPos == 5) { // end of cmd
			spiBufferPos = 0;
			cntUndefined = 0;
			if ((spiBuffer[2] & 0x0F) == 1) {
				motors_PWM_set(spiBuffer[3], (int8_t) spiBuffer[4]);
				_pwm_timeout = POWER_OFF_PWM_TIMEOUT;
			}

			if ((spiBuffer[2] & 0x10) != 0) { //on
				_torch_timeout = POWER_OFF_TORCH_TIMEOUT;
#ifdef LED_ENABLE
				DBG_LED_PORT->BRR = DBG_LED_PIN;
#endif
				GPIOA->BSRR = GPIO_Pin_7;
			} else { // off
#ifdef LED_ENABLE
				DBG_LED_PORT->BSRR = DBG_LED_PIN;
#endif
				GPIOA->BRR = GPIO_Pin_7;
			}
			USART_DBG_printf("spi2 cmd:[%d, %d, %d]\n\r", spiBuffer[2],
					spiBuffer[3], (int8_t) spiBuffer[4]);
		}
		SPI2->DR = battery_monitor_get();
	}
}
