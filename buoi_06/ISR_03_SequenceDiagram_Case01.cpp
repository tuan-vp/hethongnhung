#include "main.h"
#include "cmsis_os.h"
#include "CLCD_I2C.h"
#include "stdio.h"

/******************
	SV chu y, bat buoc
	remap I2C1 sang
	SCL -> PB08
	SDA -> PB09
	trong STM32CudeMX
	*****************/

uint16_t MS_time = 1000;
uint32_t debug_notify_value;
uint16_t task_PC13_run = 0;
char txt[16];

I2C_HandleTypeDef hi2c1;
CLCD_I2C_Name LCD1;

TaskHandle_t HTN_B06_PB05_taskhandle = NULL;
TaskHandle_t HTN_B06_PC13_taskhandle = NULL;
TaskHandle_t HTN_B06_LCD_taskhandle  = NULL;

void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_I2C1_Init(void);
void Check_LCD_address(I2C_HandleTypeDef *, const uint8_t, int, int);

void HTN_B06_LedPB05(void * arg);
void HTN_B06_LedPC13(void * arg);
void HTN_B06_I2C1LCD(void * arg);

int main(void){
  HAL_Init();
  SystemClock_Config();
  MX_GPIO_Init();
  MX_I2C1_Init();
	
  Check_LCD_address(&hi2c1, 0x4E, 3, 100);
	CLCD_I2C_Init(&LCD1,&hi2c1, 0x4E, 16, 2);

	xTaskCreate(HTN_B06_LedPB05,"Led PB05",128, NULL,
		tskIDLE_PRIORITY + 1, &HTN_B06_PB05_taskhandle);
	xTaskCreate(HTN_B06_LedPC13,"Led PC13",128, NULL,
		tskIDLE_PRIORITY + 2, &HTN_B06_PC13_taskhandle);
	xTaskCreate(HTN_B06_I2C1LCD,"I2C-LCD1 remap",128, NULL,
		tskIDLE_PRIORITY + 1, &HTN_B06_LCD_taskhandle);
  osKernelStart();
}

void SystemClock_Config(void){
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
}

static void MX_I2C1_Init(void){
  hi2c1.Instance = I2C1;
  hi2c1.Init.ClockSpeed = 100000;
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }
}
void Check_LCD_address(I2C_HandleTypeDef *hi2c_n, const uint8_t addr, int trails, int timeout){
	if(HAL_I2C_IsDeviceReady(hi2c_n, addr, trails, timeout) == HAL_OK)
	{
		for (int i = 0; i<7; i ++) // check whether I2C address is correct?
		{
		// I2C OK ? fast led blink
		HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
		HAL_Delay(100);
		}
	}
	else
	{
		for (int i = 0; i<3; i ++) // check whether I2C address is correct?
		{
			// I2C FAIL ? slow led blink
			HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
			HAL_Delay(500);
		}
	}
}
static void MX_GPIO_Init(void){
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);

  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_RESET);

  GPIO_InitStruct.Pin = GPIO_PIN_13;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = GPIO_PIN_5;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
	
	// PA10 - Button EXTI
	GPIO_InitStruct.Pin  = GPIO_PIN_10;
	GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
	GPIO_InitStruct.Pull = GPIO_PULLUP;
	HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

	// EXTI10 in EXTI15_10
	HAL_NVIC_SetPriority(EXTI15_10_IRQn, 5, 0);
	HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);

}

void HTN_B06_LedPB05(void * arg){
	for(;;){
		HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_5);
		vTaskDelay(pdMS_TO_TICKS(MS_time));
	}
}
void HTN_B06_LedPC13(void * arg){
	for(;;)
	{
		debug_notify_value = ulTaskNotifyTake(pdTRUE, portMAX_DELAY); // block task
		task_PC13_run = 1;
		while(task_PC13_run)
		{
			HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
			vTaskDelay(pdMS_TO_TICKS(550));

			if(ulTaskNotifyTake(pdTRUE, 0))
				task_PC13_run = 0;
		}
	}
}
void HTN_B06_I2C1LCD(void * arg){
	for(;;){
		CLCD_I2C_Clear(&LCD1);
		CLCD_I2C_SetCursor(&LCD1, 0,0);
		CLCD_I2C_WriteString(&LCD1, "task PC13: ");
		CLCD_I2C_SetCursor(&LCD1, 11,0);
		if(task_PC13_run)
			CLCD_I2C_WriteString(&LCD1, "RUN ");
		else
			CLCD_I2C_WriteString(&LCD1, "BLK ");
		sprintf(txt,"%u",task_PC13_run);
		CLCD_I2C_SetCursor(&LCD1, 15,0);
		CLCD_I2C_WriteString(&LCD1, txt);
		
		CLCD_I2C_SetCursor(&LCD1, 0,1);
		CLCD_I2C_WriteString(&LCD1, "PRV Notify: ");
			
		CLCD_I2C_SetCursor(&LCD1, 12,1);
		sprintf(txt,"%u",debug_notify_value);
		CLCD_I2C_WriteString(&LCD1, txt);

		vTaskDelay(pdMS_TO_TICKS(500));
	}
}

void EXTI15_10_IRQHandler(void){
   HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_10);
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin){		
	static uint32_t last_tick = 0;
	uint32_t now = xTaskGetTickCountFromISR();
	BaseType_t xHigherPriorityTaskWoken = pdFALSE;

	if (GPIO_Pin == GPIO_PIN_10)
	{
		if (now - last_tick > pdMS_TO_TICKS(400)) // debounce 400ms
		{
			MS_time = 1100 - MS_time;
			last_tick = now;
			vTaskNotifyGiveFromISR(HTN_B06_PC13_taskhandle,&xHigherPriorityTaskWoken);
			portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
		}
	}
}
void Error_Handler(void){
  __disable_irq();
  while (1)
  {
  }
}

#ifdef  USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
}
#endif
