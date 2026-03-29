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

void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_I2C1_Init(void);
void check_LCD_address(I2C_HandleTypeDef *hi2c, const uint8_t addr, int trails, int timeout);

void ISR_QPush_A10_task(void *params);
void ISR_QPop_A14_task(void *params);
void ISR_Qclear_B15_task(void *params);
void I2C1_LCD_task(void * params);

void QueuePUSH_task(void * params);
void QueuePOP_task(void * params);
void QueueCLEAR_task(void * params);

typedef enum {lcdPUSH, lcdPOP, lcdCLEAR, lcdNONE} lcd_status;
typedef enum  {PUSH, POP, CLEAR, NONE} button_action;
typedef enum {MOT,HAI,BA_,BON,NAM,SAU} dice;
typedef enum {FULL, EMPTY, OKOK} queue_status;
typedef struct{ uint8_t index; dice number; }dice_trails;

TaskHandle_t 	QueuePush_handle = NULL, QueuePop_handle = NULL, 
							QueueClear_handle = NULL, LCD_I2C1_handle = NULL;
QueueHandle_t DiceQueue;
button_action global_action = NONE; dice_trails global_dice = {99,HAI}; lcd_status global_lcdstatus = lcdNONE;
queue_status global_queuestatus = EMPTY;

int main(void)
{
  HAL_Init();
  SystemClock_Config();
  MX_GPIO_Init();
  MX_I2C1_Init();
	
	check_LCD_address(&hi2c1, 0x4E, 3, 100);
	CLCD_I2C_Init( &LCD1, &hi2c1, 0x4E, 16, 2);
	
	DiceQueue = xQueueCreate(5,sizeof(dice_trails));
	
	xTaskCreate(I2C1_LCD_task, "LCD display", 128, NULL, 2, &LCD_I2C1_handle);	
	xTaskCreate(QueuePUSH_task, "PUSH task", 128, NULL, 2, &QueuePush_handle);
	xTaskCreate(QueuePOP_task, "POP task", 128, NULL, 2, &QueuePop_handle);
	xTaskCreate(QueueCLEAR_task, "CLEAR task", 128, NULL, 2, &QueueClear_handle);
	
  osKernelStart();

  while (1){ }
}
void QueuePUSH_task(void * params){
//	srand(time(NULL));  // seed
	static uint8_t count = 0;
	BaseType_t qStatus = pdPASS;
	for(;;){
		if(global_action == PUSH){
			global_dice.index = count;
			global_dice.number = (dice)(rand()%6);
			count ++;
			qStatus = xQueueSend(DiceQueue, &global_dice, 0);
			if(qStatus ==  pdPASS){ // pdPASS or errQUEUE_FULL
				global_queuestatus = OKOK;
			}else{ 
				global_queuestatus = FULL;
			}
			global_lcdstatus = lcdPUSH ; 
			global_action = NONE;
		}
		vTaskDelay(pdMS_TO_TICKS(200));
	}
}
void QueuePOP_task(void * params){
	for(;;){
		if(global_action == POP){
			if(xQueueReceive(DiceQueue, &global_dice, 0) == pdPASS){ // pdPASS or pdFALSE (empty)
				global_queuestatus = OKOK;
			}else{
				global_queuestatus = EMPTY;
			}
			global_lcdstatus = lcdPOP ; 
			global_action = NONE;
		}
		vTaskDelay(pdMS_TO_TICKS(200));
	}
}
void QueueCLEAR_task(void * params){
	for(;;){
		if(global_action == CLEAR){
			while(xQueueReceive(DiceQueue, &global_dice, 0)){
			}
			global_lcdstatus = lcdCLEAR ; 
			global_action = NONE;
		}
		vTaskDelay(pdMS_TO_TICKS(200));
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
	if (now - last_tick > pdMS_TO_TICKS(400)) // debounce 400ms
	{
		last_tick = now;
		if( GPIO_Pin == GPIO_PIN_10){       
			global_action = POP;
		}else if(GPIO_Pin == GPIO_PIN_14){
			global_action = PUSH;
		}else if(GPIO_Pin == GPIO_PIN_15){
			global_action = CLEAR;
		}
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
	sprintf(lcd_text,"%u",pv->index);
	CLCD_I2C_WriteString(&LCD1, lcd_text);
	CLCD_I2C_WriteString(&LCD1,",");
	switch(pv->number){
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
		
		if(global_queuestatus == OKOK)
			CLCD_I2C_WriteString(&LCD1, "Queue: OKAY");
		else if(global_queuestatus == EMPTY)
			CLCD_I2C_WriteString(&LCD1, "Queue: EMPTY");
		else if(global_queuestatus == FULL)
			CLCD_I2C_WriteString(&LCD1, "Queue: FULL");	
		
		CLCD_I2C_SetCursor(&LCD1,0,1);	
		if(global_lcdstatus == lcdNONE){
			CLCD_I2C_WriteString(&LCD1, "Waiting button");
		}else if(global_lcdstatus == lcdPUSH){
			CLCD_I2C_WriteString(&LCD1, "PUSH [");
			Dice_LCD_display(6, 1, &global_dice);
		}else if(global_lcdstatus == lcdPOP){
			CLCD_I2C_WriteString(&LCD1, "POP  [");
			Dice_LCD_display(6, 1, &global_dice);
		}else{ // lcdCLEAR
			CLCD_I2C_WriteString(&LCD1, "CLEARED QUEUE");
		}
		
		vTaskDelay(pdMS_TO_TICKS(200));
	}
}

void SystemClock_Config(void)
{
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

static void MX_I2C1_Init(void)
{
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

static void MX_GPIO_Init(void)
{
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

void StartDefaultTask(void const * argument)
{
  for(;;)
  {
    osDelay(1);
  }
}

void Error_Handler(void)
{
  __disable_irq();
  while (1){  }
}

#ifdef  USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{}
#endif /* USE_FULL_ASSERT */
