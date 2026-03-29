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

typedef enum {lcdPUSH, lcdPOP, lcdCLEAR, lcdNONE} lcd_status;
typedef enum  {PUSH, POP, CLEAR, NONE} button_action;
typedef enum {MOT,HAI,BA_,BON,NAM,SAU} dice;
typedef enum {FULL, EMPTY, OKOK} queue_status;
typedef struct{ uint8_t index; dice number; }dice_trails;

void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_I2C1_Init(void);
void check_LCD_address(I2C_HandleTypeDef *hi2c, const uint8_t addr, int trails, int timeout);
void I2C1_LCD_task(void * params);
void Button_task(void * params);
void Led_task(void *params);

TaskHandle_t 	Button_andle, LCD_I2C1_handle, Led_handle;
SemaphoreHandle_t dataMutex;
QueueHandle_t DiceQueue, Buttonqueue;
dice_trails global_dice = {99,HAI}; lcd_status global_lcdstatus = lcdNONE;
queue_status global_queuestatus = EMPTY;

int main(void){
  HAL_Init();
  SystemClock_Config();
  MX_GPIO_Init();
  MX_I2C1_Init();
	
	check_LCD_address(&hi2c1, 0x4E, 3, 100);
	CLCD_I2C_Init( &LCD1, &hi2c1, 0x4E, 16, 2);
	
	Buttonqueue = xQueueCreate(5,sizeof(button_action));
	DiceQueue = xQueueCreate(5,sizeof(dice_trails));
	
	dataMutex = xSemaphoreCreateMutex();
	xTaskCreate(I2C1_LCD_task, "LCD display"	, 128, NULL, 2, &LCD_I2C1_handle);	
	xTaskCreate(Button_task, "Button Proccess", 128, NULL, 2, &Button_andle);
	xTaskCreate(Led_task, "Led Notification"	, 128, NULL, 3, &Led_handle);
	
  osKernelStart();

  while (1){  }
}
void Led_task(void * params){
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
void Button_task(void * params){
//	srand(time(NULL));  // seed
	button_action receiveAction;
	static uint8_t count = 0;
	BaseType_t qStatus;
	dice_trails 	new_dice;
	queue_status 	new_qStatus;
	lcd_status 		new_lcdStatus;
	for(;;){
		if(xQueueReceive(Buttonqueue, &receiveAction, portMAX_DELAY)==pdTRUE){
			switch(receiveAction){
				case PUSH: 
					new_dice.index = count;
					new_dice.number = (dice)(rand()%6);
						xSemaphoreTake(dataMutex, portMAX_DELAY);
						global_dice = new_dice;
						xSemaphoreGive(dataMutex);
					count ++;
					qStatus = xQueueSend(DiceQueue, &new_dice, 0);
					if(qStatus ==  pdPASS){ // pdPASS or errQUEUE_FULL
						new_qStatus = OKOK;
					}else{ 
						new_qStatus = FULL;
					}
					new_lcdStatus = lcdPUSH ; 
					break;
				case POP:
					if(xQueueReceive(DiceQueue, &global_dice, 0) == pdPASS){ // pdPASS or pdFALSE (empty)
						new_qStatus = OKOK;
					}else{
						new_qStatus = EMPTY;
					}
					new_lcdStatus = lcdPOP ; 
					break;
				case CLEAR: 
					while(xQueueReceive(DiceQueue, &global_dice, 0) == pdPASS){}
					new_lcdStatus = lcdCLEAR ; 
					break;
				case NONE: break;
			}
			xSemaphoreTake(dataMutex, portMAX_DELAY);
			global_queuestatus 	= new_qStatus;
			global_lcdstatus 		= new_lcdStatus; 
			xSemaphoreGive(dataMutex);
		}
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
	static uint32_t last_tick = 0;
	uint32_t now = xTaskGetTickCountFromISR();
	button_action currAction = NONE;
	BaseType_t xHPTW = pdFALSE;
	if (now - last_tick > pdMS_TO_TICKS(400)) // debounce 400ms
	{
		last_tick = now;
		if( GPIO_Pin == GPIO_PIN_10){       
			currAction = POP;
			xTaskNotifyFromISR(Led_handle, 1<<0, eSetBits, &xHPTW);
		}else if(GPIO_Pin == GPIO_PIN_14){
			currAction = PUSH;
			xTaskNotifyFromISR(Led_handle, 1<<1, eSetBits, &xHPTW);
		}else if(GPIO_Pin == GPIO_PIN_15){
			currAction= CLEAR;
			xTaskNotifyFromISR(Led_handle, 1<<2, eSetBits, &xHPTW);
		}
		xQueueSendFromISR(Buttonqueue, &currAction, &xHPTW);
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
void Dice_LCD_display(uint8_t xPos, uint8_t yPos, dice_trails* pv){
	CLCD_I2C_SetCursor(&LCD1, xPos, yPos);
		xSemaphoreTake(dataMutex,portMAX_DELAY);
		sprintf(lcd_text,"%u",pv->index);
		dice pv_dice = pv->number;
		xSemaphoreGive(dataMutex);
	CLCD_I2C_WriteString(&LCD1, lcd_text);
	CLCD_I2C_WriteString(&LCD1,",");
	switch(pv_dice){
		case MOT: CLCD_I2C_WriteString(&LCD1, "MOT]"); break;
		case HAI: CLCD_I2C_WriteString(&LCD1, "HAI]"); break;
		case BA_: CLCD_I2C_WriteString(&LCD1, "BA]"); break;
		case BON: CLCD_I2C_WriteString(&LCD1, "BON]"); break;
		case NAM: CLCD_I2C_WriteString(&LCD1, "NAM]"); break;
		case SAU: CLCD_I2C_WriteString(&LCD1, "SAU]"); break;
	}
}
void I2C1_LCD_task(void *params){
	for(;;){
		CLCD_I2C_Clear(&LCD1);
		CLCD_I2C_SetCursor(&LCD1,0,0);	
		queue_status 	new_qStatus;
		lcd_status 		new_lcdStatus;
			xSemaphoreTake(dataMutex, portMAX_DELAY);
			new_lcdStatus 	= global_lcdstatus;
			new_qStatus 		= global_queuestatus;
			xSemaphoreGive(dataMutex);
		if(new_qStatus == OKOK)
			CLCD_I2C_WriteString(&LCD1, "Queue: OKAY");
		else if(new_qStatus == EMPTY)
			CLCD_I2C_WriteString(&LCD1, "Queue: EMPTY");
		else if(new_qStatus == FULL)
			CLCD_I2C_WriteString(&LCD1, "Queue: FULL");	
		
		CLCD_I2C_SetCursor(&LCD1,0,1);	
		if(new_lcdStatus == lcdNONE){
			CLCD_I2C_WriteString(&LCD1, "Waiting button");
		}else if(new_lcdStatus == lcdPUSH){
			CLCD_I2C_WriteString(&LCD1, "PUSH [");
			Dice_LCD_display(6, 1, &global_dice);
		}else if(new_lcdStatus == lcdPOP){
			CLCD_I2C_WriteString(&LCD1, "POP  [");
			Dice_LCD_display(6, 1, &global_dice);
		}else{ // lcdCLEAR
			CLCD_I2C_WriteString(&LCD1, "CLEARED QUEUE");
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
