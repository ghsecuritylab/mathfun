
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * This notice applies to any and all portions of this file
  * that are not between comment pairs USER CODE BEGIN and
  * USER CODE END. Other portions of this file, whether 
  * inserted by the user or by software development tools
  * are owned by their respective copyright owners.
  *
  * Copyright (c) 2018 STMicroelectronics International N.V. 
  * All rights reserved.
  *
  * Redistribution and use in source and binary forms, with or without 
  * modification, are permitted, provided that the following conditions are met:
  *
  * 1. Redistribution of source code must retain the above copyright notice, 
  *    this list of conditions and the following disclaimer.
  * 2. Redistributions in binary form must reproduce the above copyright notice,
  *    this list of conditions and the following disclaimer in the documentation
  *    and/or other materials provided with the distribution.
  * 3. Neither the name of STMicroelectronics nor the names of other 
  *    contributors to this software may be used to endorse or promote products 
  *    derived from this software without specific written permission.
  * 4. This software, including modifications and/or derivative works of this 
  *    software, must execute solely and exclusively on microcontroller or
  *    microprocessor devices manufactured by or for STMicroelectronics.
  * 5. Redistribution and use of this software other than as permitted under 
  *    this license is void and will automatically terminate your rights under 
  *    this license. 
  *
  * THIS SOFTWARE IS PROVIDED BY STMICROELECTRONICS AND CONTRIBUTORS "AS IS" 
  * AND ANY EXPRESS, IMPLIED OR STATUTORY WARRANTIES, INCLUDING, BUT NOT 
  * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A 
  * PARTICULAR PURPOSE AND NON-INFRINGEMENT OF THIRD PARTY INTELLECTUAL PROPERTY
  * RIGHTS ARE DISCLAIMED TO THE FULLEST EXTENT PERMITTED BY LAW. IN NO EVENT 
  * SHALL STMICROELECTRONICS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
  * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
  * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, 
  * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF 
  * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING 
  * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
  * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
  *
  ******************************************************************************
  */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "stm32f4xx_hal.h"
#include "usb_device.h"

/* USER CODE BEGIN Includes */

#include "math.h"
#include "usbd_cdc_if.h"
#include "epos_can.h"
#include "dwt_stm32_delay.h"

/* USER CODE END Includes */

/* Private variables ---------------------------------------------------------*/
CAN_HandleTypeDef hcan1;

I2C_HandleTypeDef hi2c1;

TIM_HandleTypeDef htim3;

/* USER CODE BEGIN PV */
/* Private variables ---------------------------------------------------------*/

const uint16_t adxl_device_a_address=(0x53<<1);
const uint16_t adxl_device_b_address=(0x1d<<1);

volatile uint8_t system_task=0;

volatile int16_t epos_current =0;
volatile int32_t epos_position=0;
volatile int32_t capture3=0, capture3_prev=0, encoder3=0;

volatile uint32_t useconds=0, useconds_prev=0, time_of_start=0;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_CAN1_Init(void);
static void MX_TIM3_Init(void);
static void MX_I2C1_Init(void);

/* USER CODE BEGIN PFP */
/* Private function prototypes -----------------------------------------------*/

/* USER CODE END PFP */

/* USER CODE BEGIN 0 */
void adxl_write(const uint16_t adxl_address, uint8_t reg, uint8_t value) {
  uint8_t data[2];
  data[0] = reg;
  data[1] = value;
  HAL_I2C_Master_Transmit(&hi2c1, adxl_address, data, 2, 100);
}

void adxl_read_values(const uint16_t adxl_address, uint8_t reg, uint8_t *buf) {
  HAL_I2C_Mem_Read(&hi2c1, adxl_address, reg, 1, (uint8_t *)buf, 6, 3);
}

void get_adxl_xyz(const uint16_t adxl_address, float *x, float *y, float *z) {
  uint8_t data_rec[6];
  adxl_read_values(adxl_address, 0x32, data_rec);
  int16_t ix = ((data_rec[1]<<8)|data_rec[0]);
  int16_t iy = ((data_rec[3]<<8)|data_rec[2]);
  int16_t iz = ((data_rec[5]<<8)|data_rec[4]);
  *x = ix * .0039;
  *y = iy * .0039;
  *z = iz * .0039;
}


float get_adxl_angle_(const uint16_t adxl_address) {
  float x,y,z;
  get_adxl_xyz(adxl_address, &x, &y, &z);
  float angle = atan2(x,y);
  return angle;
}

float get_adxl_angle() {
  float ax,ay,az,bx,by,bz;
  get_adxl_xyz(adxl_device_a_address, &ax, &ay, &az);
  get_adxl_xyz(adxl_device_b_address, &bx, &by, &bz);
  const float mu = 2.;
  return -atan2(ax-mu*bx,-ay+mu*by);
}

uint8_t adxl_read_address(const uint16_t adxl_address, uint8_t reg) {
  uint8_t val = 0;
  HAL_I2C_Mem_Read(&hi2c1, adxl_address, reg, 1, &val, 1, 3);
  return val;
}

void adxl_init (const uint16_t adxl_address) {
  adxl_read_address(adxl_address, 0x00); // read the DEVID
  adxl_write(adxl_address, 0x31, 0x00);  // data_format range= +- 4g
  adxl_write(adxl_address, 0x2d, 0x00);  // reset all bits
  adxl_write(adxl_address, 0x2d, 0x08);  // power_cntl measure and wake up 8hz
  adxl_write(adxl_address, 0x2c, 0x0f);  // 3200 Hz output data rate
}


void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim) {
	if (htim->Instance==TIM4) {
	  /*
		uint16_t LEDS[4] = {GPIO_PIN_12, GPIO_PIN_13, GPIO_PIN_14, GPIO_PIN_15};
		HAL_GPIO_TogglePin(GPIOD, LEDS[led]);
		led = (led<3 ? led+1 : 0);
	   */
	}
}

void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan) {
  CAN_RxHeaderTypeDef RxHeader;
  uint8_t RxData[8] = { 0 };
  if (HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &RxHeader, RxData) != HAL_OK) {
    _Error_Handler(__FILE__, __LINE__);
  }
  if ((RxHeader.StdId == 0x181) && (RxHeader.IDE == CAN_ID_STD) && (RxHeader.DLC == 6)) {
    HAL_GPIO_TogglePin(GPIOD, GPIO_PIN_13);
    epos_current  = *(int16_t *)(RxData);
    epos_position = *(int32_t *)(RxData+2);
  }
}

void can_configure() {
  CAN_FilterTypeDef sFilterConfig;
  sFilterConfig.FilterBank = 0;
  sFilterConfig.FilterMode = CAN_FILTERMODE_IDMASK;
  sFilterConfig.FilterScale = CAN_FILTERSCALE_32BIT;
  sFilterConfig.FilterIdHigh = 0x0000;
  sFilterConfig.FilterIdLow = 0x0000;
  sFilterConfig.FilterMaskIdHigh = 0x0000;
  sFilterConfig.FilterMaskIdLow = 0x0000;
  sFilterConfig.FilterFIFOAssignment = CAN_RX_FIFO0;
  sFilterConfig.FilterActivation = ENABLE;
  sFilterConfig.SlaveStartFilterBank = 14;

  if (HAL_CAN_ConfigFilter(&hcan1, &sFilterConfig) != HAL_OK)
    _Error_Handler(__FILE__, __LINE__);

  if (HAL_CAN_Start(&hcan1) != HAL_OK)
    _Error_Handler(__FILE__, __LINE__);

  if (HAL_CAN_ActivateNotification(&hcan1, CAN_IT_RX_FIFO0_MSG_PENDING) != HAL_OK)
    _Error_Handler(__FILE__, __LINE__);
}

float get_rw_angle() {
  return epos_position * (2.*M_PI / 48.);
}

float get_current() {
  return epos_current/1000.;
}

void set_current(float ref) {
  setCurrent(0x201, ref*1000);
}

float get_pendulum_angle(uint8_t mod2p) {
  // handle eventual overflows in the encoder reading
  capture3 = TIM3->CNT;
  encoder3 += capture3 - capture3_prev;
  if (labs(capture3 - capture3_prev) > 32767) {
    encoder3 += (capture3 < capture3_prev ? 65535 : -65535);
  }
  capture3_prev = capture3;

  float encoder_angle = (encoder3 - 5000) * (2.*M_PI/10000.);
  if (mod2p) {
    while (encoder_angle >  M_PI) encoder_angle -= 2.*M_PI; // mod 2pi
    while (encoder_angle < -M_PI) encoder_angle += 2.*M_PI;
  }
  return encoder_angle;
}

// frequencies are given in mHz
void chirp_current_open_loop(float amplitude, float start_freq, float end_freq, uint32_t duration_ms) {
  char buff[1024] = "time(s),reference current(A),current(A),rw angle(rad),pendulum angle(rad)\r\n";
  CDC_Transmit_FS((uint8_t *)buff, strlen(buff));

  uint32_t time_of_start, useconds;
  useconds = time_of_start = DWT_us();

  while (useconds < time_of_start + duration_ms * 1000L) {
    useconds = DWT_us();
    float t = (useconds - time_of_start) * 0.000001;
    float phi = (start_freq + (end_freq - start_freq) * (t*1000.) / (float) duration_ms) * t;
    float current_ref = sin(2. * M_PI * phi) * amplitude;
    set_current(current_ref);

    float current_measured = get_current();
    float mesQr = get_rw_angle();
    float mesQ  = get_pendulum_angle(0);


    sprintf(buff, "%3.6f, %3.6f, %3.6f, %3.6f, %3.6f\r\n", (useconds - time_of_start) * 1e-6, current_ref, current_measured, mesQr, mesQ);
    CDC_Transmit_FS((uint8_t *)buff, strlen(buff));
    while (DWT_us() - useconds < 2000);
  }
  set_current(0);
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  *
  * @retval None
  */
int main(void)
{
  /* USER CODE BEGIN 1 */
  /* USER CODE END 1 */

  /* MCU Configuration----------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  if (DWT_Delay_Init() != HAL_OK)
    _Error_Handler(__FILE__, __LINE__);

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_CAN1_Init();
  MX_USB_DEVICE_Init();
  MX_TIM3_Init();
  MX_I2C1_Init();
  /* USER CODE BEGIN 2 */

  HAL_TIM_Encoder_Start(&htim3, TIM_CHANNEL_ALL);

//  adxl_init(adxl_device_a_address);
//  adxl_init(adxl_device_b_address);

  can_configure();
  eposReset();
  HAL_Delay(1000);
  enableEpos(0x01);
  enterOperational();

 // HAL_Delay(5000);

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */

  float hatX1  = 0., hatX2  = 0., hatX3  = 0.;
  float hatXp1 = 0., hatXp2 = 0., hatXr1 = 0., hatXr2 = 0.;
  float Z = .0;

  while (1) {
    if (0 && system_task) {
      HAL_GPIO_TogglePin(GPIOD, GPIO_PIN_12);
      HAL_Delay(3000);
      chirp_current_open_loop(5., .5, 2., 10000);
      system_task = 0;
    }

    float mesQ  = get_pendulum_angle(1);
    float mesQr = get_rw_angle();
    float mesI  = get_current();

    useconds_prev = useconds;
    useconds = DWT_us();

    if (system_task != 2) {
      char buff2[255];
      sprintf(buff2, "working ok, timestamp: %lu, task: %d %f %f\r\n", DWT_us(), system_task, mesQ, mesQr);
      CDC_Transmit_FS((uint8_t *) buff2, strlen(buff2));
      set_current(0);
    }

  /* USER CODE END WHILE */

  /* USER CODE BEGIN 3 */

    if (1 == system_task) {
      hatX1 = mesQ;
      hatX2 = hatX3 = 0.;

      hatXp1 = mesQ;
      hatXr1 = mesQr;
      hatXr2 = hatXp2 = 0.;

      if (fabs(mesQ) < .1) {
        system_task = 2;
        time_of_start = useconds;
        char buff2[255] = "#time(s),reference current(A),current(A),rw angle(rad),pendulum angle(rad)\r\n";
        CDC_Transmit_FS((uint8_t *) buff2, strlen(buff2));
      }
    }
    if (2 == system_task) {
      float dt = (useconds - useconds_prev) / 1000000.; // seconds

      const float a = .75;
      const float k1 = 7.;
      const float k2 = 12.;

      const float g = 9.81;
      const float k = 0.0369;
      const float Jr = 0.001248;
      const float mr = .35;
      const float lr = .22;

      const float Jp = 0.0038;
      const float mp = 0.578;
      const float lp = 0.10;

      const float J = Jp + mr*lr*lr + mp*lp*lp;
      const float ml = mp*lp + mr*lr;

//      const float K[] = {139., 20., 0.29};
      const float K[] = {581.6, 83.4, 1.18};
//      const float L[] = {52., 278., -31.};
//      const float L[] = {20.6260, 121.5939, -6.7414};

//      float qhatX1 = ( roundf(hatX1*5000./M_PI) ) * (M_PI/5000.);
//      float qhatX3 = ( roundf(hatX3*5000./M_PI) ) * (M_PI/5000.);


      float sinQ = sin(mesQ - hatX3);
      float friction = 0.1;
      float effI = mesI - (hatXr2 > 0 ? friction : -friction);

      float ep = hatXp1 - (mesQ - hatX3);
      float dhatXp1 = hatXp2                  - k1*pow(fabs(ep),       a)*(ep>0?1.:-1.);
      float dhatXp2 = -k/J*effI + ml*g/J*sinQ - k2*pow(fabs(ep), 2.*a-1.)*(ep>0?1.:-1.);
      hatXp1 += dt*dhatXp1;
      hatXp2 += dt*dhatXp2;

      float er = hatXr1 - mesQr;
      float dhatXr1 = hatXr2                             - k1*pow(fabs(er),       a)*(er>0?1.:-1.);
      float dhatXr2 = (J+Jr)/(J*Jr)*k*effI - ml*g/J*sinQ - k2*pow(fabs(er), 2.*a-1.)*(er>0?1.:-1.);
      hatXr1 += dt*dhatXr1;
      hatXr2 += dt*dhatXr2;


      float a1 = ml*g/J;
      float b1 = -k/J;
      float L = -.033;

      Z += dt*(-a1*sinQ-b1*effI);
      hatX3 = L*(hatXp2 + Z);


#if 0
      float ytilde = hatX1 + hatX3 - mesQ;

      float dhatX1 = hatX2 - L[0]*ytilde;
      float dhatX2 = b1*(mesI - (hatXr2 > 0 ? friction : -friction)) + a1*sinQ - L[1]*ytilde;
      float dhatX3 = -L[2]*ytilde;

      hatX1 += dt*dhatX1;
      hatX2 += dt*dhatX2;
      if (fabs(ytilde)>4*2.*M_PI/10000.)
      hatX3 += dt*dhatX3;
#endif


//      int int_hatX3 = (int)(hatX3 * 10000./(2.*M_PI) + 5000);
//      hatX3 = (int_hatX3 - 5000) * (2.*M_PI/10000.);



//      float refI = K[0]*mesQ + K[1]*hatXp2 + K[2]*hatXr2;
      float refI = K[0]*(mesQ-hatX3) + K[1]*hatXp2 + K[2]*hatXr2;
      if (refI >  5.) refI =  5.;
      if (refI < -5.) refI = -5.;

      set_current(refI);

      if (fabs(get_pendulum_angle(1)) > .3) {
        system_task = 9;
        set_current(0);
      }

      char buff[1024] = { 0 };
//      sprintf(buff, "%3.5f, %3.5f, %3.5f, %3.5f, %3.5f, %3.5f, %3.5f, %3.5f, %3.5f, %3.5f, %3.5f, %3.5f\r\n", (useconds - time_of_start)*1e-6, refI, mesI, mesQr, mesQ, hatX1, hatX2, hatX3, hatXp1, hatXp2, hatXr1, hatXr2);
       sprintf(buff, "%3.6f, %3.6f, %3.6f, %3.6f, %3.6f\r\n", (useconds - time_of_start)*1e-6, refI, mesI, mesQr, mesQ);
      CDC_Transmit_FS((uint8_t *) buff, strlen(buff));
    }

    while (DWT_us() - useconds < 2000);
  }
  /* USER CODE END 3 */

}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{

  RCC_OscInitTypeDef RCC_OscInitStruct;
  RCC_ClkInitTypeDef RCC_ClkInitStruct;

    /**Configure the main internal regulator output voltage 
    */
  __HAL_RCC_PWR_CLK_ENABLE();

  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    /**Initializes the CPU, AHB and APB busses clocks 
    */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 7;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    _Error_Handler(__FILE__, __LINE__);
  }

    /**Initializes the CPU, AHB and APB busses clocks 
    */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    _Error_Handler(__FILE__, __LINE__);
  }

    /**Configure the Systick interrupt time 
    */
  HAL_SYSTICK_Config(HAL_RCC_GetHCLKFreq()/1000);

    /**Configure the Systick 
    */
  HAL_SYSTICK_CLKSourceConfig(SYSTICK_CLKSOURCE_HCLK);

  /* SysTick_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(SysTick_IRQn, 0, 0);
}

/* CAN1 init function */
static void MX_CAN1_Init(void)
{

  hcan1.Instance = CAN1;
  hcan1.Init.Prescaler = 21;
  hcan1.Init.Mode = CAN_MODE_NORMAL;
  hcan1.Init.SyncJumpWidth = CAN_SJW_1TQ;
  hcan1.Init.TimeSeg1 = CAN_BS1_13TQ;
  hcan1.Init.TimeSeg2 = CAN_BS2_2TQ;
  hcan1.Init.TimeTriggeredMode = DISABLE;
  hcan1.Init.AutoBusOff = DISABLE;
  hcan1.Init.AutoWakeUp = DISABLE;
  hcan1.Init.AutoRetransmission = DISABLE;
  hcan1.Init.ReceiveFifoLocked = DISABLE;
  hcan1.Init.TransmitFifoPriority = DISABLE;
  if (HAL_CAN_Init(&hcan1) != HAL_OK)
  {
    _Error_Handler(__FILE__, __LINE__);
  }

}

/* I2C1 init function */
static void MX_I2C1_Init(void)
{

  hi2c1.Instance = I2C1;
  hi2c1.Init.ClockSpeed = 400000;
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    _Error_Handler(__FILE__, __LINE__);
  }

}

/* TIM3 init function */
static void MX_TIM3_Init(void)
{

  TIM_Encoder_InitTypeDef sConfig;
  TIM_MasterConfigTypeDef sMasterConfig;

  htim3.Instance = TIM3;
  htim3.Init.Prescaler = 0;
  htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim3.Init.Period = 65535;
  htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  sConfig.EncoderMode = TIM_ENCODERMODE_TI12;
  sConfig.IC1Polarity = TIM_ICPOLARITY_RISING;
  sConfig.IC1Selection = TIM_ICSELECTION_DIRECTTI;
  sConfig.IC1Prescaler = TIM_ICPSC_DIV1;
  sConfig.IC1Filter = 15;
  sConfig.IC2Polarity = TIM_ICPOLARITY_RISING;
  sConfig.IC2Selection = TIM_ICSELECTION_DIRECTTI;
  sConfig.IC2Prescaler = TIM_ICPSC_DIV1;
  sConfig.IC2Filter = 15;
  if (HAL_TIM_Encoder_Init(&htim3, &sConfig) != HAL_OK)
  {
    _Error_Handler(__FILE__, __LINE__);
  }

  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig) != HAL_OK)
  {
    _Error_Handler(__FILE__, __LINE__);
  }

}

/** Configure pins as 
        * Analog 
        * Input 
        * Output
        * EVENT_OUT
        * EXTI
*/
static void MX_GPIO_Init(void)
{

  GPIO_InitTypeDef GPIO_InitStruct;

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOD, GPIO_PIN_12|GPIO_PIN_13|GPIO_PIN_14|GPIO_PIN_15, GPIO_PIN_RESET);

  /*Configure GPIO pin : PA0 */
  GPIO_InitStruct.Pin = GPIO_PIN_0;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : PD12 PD13 PD14 PD15 */
  GPIO_InitStruct.Pin = GPIO_PIN_12|GPIO_PIN_13|GPIO_PIN_14|GPIO_PIN_15;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI0_IRQn, 0, 1);
  HAL_NVIC_EnableIRQ(EXTI0_IRQn);

}

/* USER CODE BEGIN 4 */

int _write(int file, char *ptr, int len) {
  /* Implement your write code here, this is used by puts and printf for example */
  for (int i=0 ; i<len ; i++)
    ITM_SendChar((*ptr++));
  return len;
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @param  file: The file name as string.
  * @param  line: The line in file as a number.
  * @retval None
  */
void _Error_Handler(char *file, int line)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  HAL_GPIO_WritePin(GPIOD, GPIO_PIN_15, GPIO_PIN_SET);

  while (1) {
    __ASM volatile ("NOP");
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t* file, uint32_t line)
{ 
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     tex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */

/**
  * @}
  */

/**
  * @}
  */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
