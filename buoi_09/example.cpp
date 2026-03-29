#include "main.h"
#include "cmsis_os.h"
#include "CLCD_I2C.h"
#include "stdio.h"
#include "queue.h"
#include "stdlib.h"
#include "time.h"

I2C_HandleTypeDef hi2c1;
CLCD_I2C_Name LCD1;
char lcd_text[16];

typedef enum {lcdHumidity, lcdTemperature, lcdShowC, lcdShowF, lcdInit} lcdStatus;
typedef enum {randHumidity, randTemperture, disMode} buttonAction; // PB15, PB10, PA15;
typedef enum { humidity, temperature} sensorType;
typedef enum {FULL, EMPTY, OKOK} queueStatus;
typedef struct{ float _value; sensorType _type; } sensorStruct;

void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_I2C1_Init(void);
void sensor_LCD_display(uint8_t xPos, uint8_t yPos, sensorStruct* pv);
void check_LCD_address(I2C_HandleTypeDef *hi2c, const uint8_t addr, int trails, int timeout);
void         lcd_task(void * params);
void 		  button_task(void * params);
void          led_task(void * params);

TaskHandle_t 	button_handle, lcd_handle, led_handle;
SemaphoreHandle_t sensorMutex, lcdMutex;
QueueHandle_t tempQueue, humiQueue, buttonQueue;

sensorStruct g_tempData = {25.51, temperature}, g_humiData = {76.33, humidity}; 
lcdStatus 	g_lcdstatus = lcdInit;
queueStatus g_qStatus = EMPTY;

int main(void){
  HAL_Init();
  SystemClock_Config();
  MX_GPIO_Init();
  MX_I2C1_Init();
	
	check_LCD_address(&hi2c1, 0x4E, 3, 100);
	CLCD_I2C_Init( &LCD1, &hi2c1, 0x4E, 16, 2);
	
	buttonQueue = xQueueCreate(5,sizeof(buttonAction));
	tempQueue = xQueueCreate(10,sizeof(sensorStruct));
	humiQueue = xQueueCreate(10,sizeof(sensorStruct));
	
	sensorMutex = xSemaphoreCreateMutex();
	lcdMutex = xSemaphoreCreateMutex();
	
	xTaskCreate(lcd_task, "Read temp/humi", 128, NULL, 2, &lcd_handle);	
	xTaskCreate(button_task, "press button", 128, NULL, 2, &button_handle);
	xTaskCreate(led_task, "Led task"	, 128, NULL, 2, &led_handle);
	
  osKernelStart();
  while(1){ }
}
void led_task(void * params){
	uint32_t notifyValue;
	for(;;){
		xTaskNotifyWait(0, 0xFFFFFFFF, &notifyValue, portMAX_DELAY);
		if(notifyValue & (1<<0))
			HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_6);
		else if(notifyValue & (1<<1))
			HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_5);
		else if(notifyValue & (1<<2))
			HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_7);
	}
}

void button_task(void * params){
//	srand(time(NULL));  // seed
	buttonAction currButton;
	sensorStruct alt_tempData, alt_humiData;
	BaseType_t qStatus; // check FULL
	lcdStatus alt_ladStatus;
	
	for(;;){
		if(xQueueReceive(buttonQueue, &currButton, portMAX_DELAY) == pdTRUE){
			xSemaphoreTake(lcdMutex,portMAX_DELAY);
			alt_ladStatus = g_lcdstatus;
			xSemaphoreGive(lcdMutex);
			switch(currButton){
				case randHumidity:
						xSemaphoreTake(sensorMutex,portMAX_DELAY);
						alt_humiData = g_humiData;
						xSemaphoreGive(sensorMutex);
					alt_humiData._value = (float) (rand()%1000)/10;
					alt_humiData._type = humidity;
					qStatus = xQueueSend(humiQueue, &alt_humiData, 0);
						xSemaphoreTake(sensorMutex,portMAX_DELAY);
						g_humiData = alt_humiData;
						xSemaphoreGive(sensorMutex);
						alt_ladStatus = lcdHumidity;
					break;
				case randTemperture:
						xSemaphoreTake(sensorMutex,portMAX_DELAY);
						alt_tempData = g_tempData;
						xSemaphoreGive(sensorMutex);
					alt_tempData._value = (float) (rand()%1500-500)/10;
					alt_tempData._type = temperature;
					qStatus = xQueueSend(tempQueue, &alt_tempData, 0);
						xSemaphoreTake(sensorMutex,portMAX_DELAY);
						g_tempData = alt_tempData;
						xSemaphoreGive(sensorMutex);
						alt_ladStatus = lcdTemperature;
					break;
				case disMode:
					if(alt_ladStatus == lcdShowC){
						alt_ladStatus = lcdShowF;}
					else{
						alt_ladStatus = lcdShowC;}
					break;
			}
			xSemaphoreTake(lcdMutex,portMAX_DELAY);
			g_lcdstatus = alt_ladStatus;
			xSemaphoreGive(lcdMutex);
		}
//		vTaskDelay(pdMS_TO_TICKS(200));
	}
}
void EXTI15_10_IRQHandler(void){
	if(__HAL_GPIO_EXTI_GET_IT(GPIO_PIN_10)){    // EXTI->PR & GPIO_PIN_10
		HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_10);   // auto call	__HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_10);
	}
	if(__HAL_GPIO_EXTI_GET_IT(GPIO_PIN_14)){    // EXTI->PR & GPIO_PIN_14
		HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_14);   //__HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_14);
	}
	if(__HAL_GPIO_EXTI_GET_IT(GPIO_PIN_15)){   // EXTI->PR & GPIO_PIN_15
		HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_15);  // __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_15);
	}
}
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin){
	buttonAction currAction;
	BaseType_t xHPTW = pdFALSE;
	static uint32_t last_tick = 0;
	uint32_t now = xTaskGetTickCountFromISR();
	if (now - last_tick > pdMS_TO_TICKS(400)) // debounce 400ms
	{
		last_tick = now;
		if( GPIO_Pin == GPIO_PIN_10){       
			currAction = randTemperture;
			xTaskNotifyFromISR(led_handle, 1<<0, eSetBits, &xHPTW);
		}else if(GPIO_Pin == GPIO_PIN_14){
			currAction = disMode;
			xTaskNotifyFromISR(led_handle, 1<<1, eSetBits, &xHPTW);
		}else if(GPIO_Pin == GPIO_PIN_15){
			currAction= randHumidity;
			xTaskNotifyFromISR(led_handle, 1<<2, eSetBits, &xHPTW);
		}
		xQueueSendFromISR(buttonQueue, &currAction, &xHPTW);
		portYIELD_FROM_ISR(xHPTW);
	}
}

void check_LCD_address(I2C_HandleTypeDef *hi2c, const uint8_t addr, int trails, int timeout){
	HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
	if( HAL_I2C_IsDeviceReady(hi2c, addr, trails, timeout) == HAL_OK){
		for(int i = 0;i<20;i++){
			HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
			HAL_Delay(50);
		}
	}else{
		for(int i = 0;i<6;i++){
			HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
			HAL_Delay(500);
		}
	}
	HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
}
void lcd_task(void *params){
//	queueStatus	alt_qStatus;
	BaseType_t qStatus;	// Check EMPTY
	lcdStatus		alt_lcdStatus;
	sensorStruct alt_tempData, alt_humiData;
	for(;;){
		CLCD_I2C_Clear(&LCD1);
		CLCD_I2C_SetCursor(&LCD1,0,0);	
			xSemaphoreTake(lcdMutex, portMAX_DELAY);
			alt_lcdStatus 	= g_lcdstatus;
			xSemaphoreGive(lcdMutex);
		
//			alt_qStatus	= g_qStatus;
			xSemaphoreTake(sensorMutex, portMAX_DELAY);
			alt_tempData = g_tempData; // FOR rand gen
			alt_humiData = g_humiData; // FOR rand gen
			xSemaphoreGive(sensorMutex);
		
		CLCD_I2C_SetCursor(&LCD1,0,1);	
		if(alt_lcdStatus == lcdInit){
			CLCD_I2C_SetCursor(&LCD1,0,0);
			CLCD_I2C_WriteString(&LCD1, "TEMP.& HUMI exp.");
			CLCD_I2C_SetCursor(&LCD1,0,1);	
			CLCD_I2C_WriteString(&LCD1, "with Queue Mutex");
		}else if((alt_lcdStatus == lcdShowC) || (alt_lcdStatus == lcdShowF)){
			qStatus = xQueueReceive(tempQueue, &alt_tempData, 0);
			if(qStatus == pdTRUE){
				CLCD_I2C_SetCursor(&LCD1,0,0);
				CLCD_I2C_WriteString(&LCD1, "Curr| ");
				if(alt_lcdStatus == lcdShowC){
					sprintf(lcd_text, "T:%.1f%cC", alt_tempData._value, 0xDF);
				}else{
					sprintf(lcd_text, "T:%.1f%cF", 32+1.8*alt_tempData._value, 0xDF);
				}
				CLCD_I2C_WriteString(&LCD1, lcd_text);
			}else{
				CLCD_I2C_SetCursor(&LCD1,0,0);
				if(alt_lcdStatus == lcdShowC){
					sprintf(lcd_text, "Curr| T: --%cC", 0xDF);
				}else{
					sprintf(lcd_text, "Curr| T: --%cF", 0xDF);
				}
				CLCD_I2C_WriteString(&LCD1, lcd_text);
			}
			
			qStatus = xQueueReceive(humiQueue, &alt_humiData, 0);
			if(qStatus == pdTRUE){
				CLCD_I2C_SetCursor(&LCD1,0,1);	
				CLCD_I2C_WriteString(&LCD1, "val_| ");
				sprintf(lcd_text, "H:%.1f%%", alt_humiData._value);
				CLCD_I2C_WriteString(&LCD1, lcd_text);
			}else{
				CLCD_I2C_SetCursor(&LCD1,0,1);	
				CLCD_I2C_WriteString(&LCD1, "val_| H: --%");
			}
		}else if(alt_lcdStatus == lcdHumidity){
			CLCD_I2C_SetCursor(&LCD1,0,0);
			CLCD_I2C_WriteString(&LCD1, "Rand Gen Humi:");
			CLCD_I2C_SetCursor(&LCD1,0,1);
			// Queue --> receive humi
			sprintf(lcd_text,"H=%.2f%%",alt_humiData._value);
			CLCD_I2C_WriteString(&LCD1, lcd_text);
		}else if(alt_lcdStatus == lcdTemperature){
			CLCD_I2C_SetCursor(&LCD1,0,0);
			CLCD_I2C_WriteString(&LCD1, "Rand Gen Temp:");
			CLCD_I2C_SetCursor(&LCD1,0,1);
			// Queue --> receive temp
			sprintf(lcd_text,"T=%.2f%cC",alt_tempData._value,0xDF);
			CLCD_I2C_WriteString(&LCD1, lcd_text);
		}
		vTaskDelay(pdMS_TO_TICKS(200));
	}
}

void SystemClock_Config(void){
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
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

static void MX_GPIO_Init(void){
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
	__HAL_RCC_GPIOC_CLK_ENABLE();
	
	
	HAL_GPIO_WritePin(GPIOB,GPIO_PIN_5,GPIO_PIN_RESET);
	HAL_GPIO_WritePin(GPIOB,GPIO_PIN_6,GPIO_PIN_RESET);
	HAL_GPIO_WritePin(GPIOB,GPIO_PIN_7,GPIO_PIN_RESET);

	HAL_GPIO_WritePin(GPIOC,GPIO_PIN_13,GPIO_PIN_RESET);
		
	GPIO_InitTypeDef InitStruct;
	InitStruct.Pin = GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7;
	InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
	InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	InitStruct.Pull = GPIO_NOPULL;
	HAL_GPIO_Init(GPIOB,&InitStruct);
	
	InitStruct.Pin = GPIO_PIN_13;
	HAL_GPIO_Init(GPIOC,&InitStruct);
	
	// EXTI PA10, PA15
	InitStruct.Pin = GPIO_PIN_10 | GPIO_PIN_15;
	InitStruct.Mode = GPIO_MODE_IT_FALLING;
	InitStruct.Pull = GPIO_PULLUP;
	HAL_GPIO_Init(GPIOA, &InitStruct);
	// EXTI PB14
	InitStruct.Pin = GPIO_PIN_14;
	HAL_GPIO_Init(GPIOB, &InitStruct);
	// Enable EXTI15_10
	HAL_NVIC_SetPriority(EXTI15_10_IRQn, 5, 0);
	HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);
}

void StartDefaultTask(void const * argument){
  for(;;)
  {
    osDelay(1);
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
{}
#endif /* USE_FULL_ASSERT */
