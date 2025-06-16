/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "fatfs.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "fatfs_sd.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "cnn_params.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
FATFS fs; // file system
FIL fil; // file
FRESULT fresult; // to store the result
char buffer[16384]; // buffer for reading lines (larger for long lines)

UINT br, bw; // file read/write count

/* capacity related variables */
FATFS *pfs;
DWORD fre_clust;
uint32_t total, free_space;

/* CIFAR-10 related variables */
volatile uint8_t image_data[32][32][3]; // RGB image data array
volatile uint8_t current_label; // Current image label
volatile uint32_t current_image_index = 0; // Current image index
volatile uint32_t total_images_read = 0; // Total images read so far
volatile float confidence;

volatile const uint32_t NUMBER_OF_FIRST_IMAGES = 10000;
volatile uint32_t number_of_true_predicted = 0;
volatile uint32_t total_inference_time = 0;

/* File names for the batch files */
const char* batch_files[] = {
    "cifar10_batch_1.txt",
    "cifar10_batch_2.txt",
    "cifar10_batch_3.txt",
    "cifar10_batch_4.txt",
    "cifar10_batch_5.txt",
    "cifar10_batch_6.txt",
    "cifar10_batch_7.txt",
    "cifar10_batch_8.txt",
    "cifar10_batch_9.txt",
    "cifar10_batch_10.txt"
};
#define NUM_BATCH_FILES (sizeof(batch_files) / sizeof(batch_files[0]))

/* Current batch file index */
volatile uint8_t current_batch = 0;

/* Flag for processing data */
volatile uint8_t ready_to_process = 0;
volatile uint8_t led_state = 0;
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
SPI_HandleTypeDef hspi1;

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_SPI1_Init(void);
/* USER CODE BEGIN PFP */

int _write(int file, char *ptr, int len);
void bufclear(void);
FRESULT open_next_batch_file(void);
int extract_number(char **ptr);
FRESULT read_cifar_oneline_image(void);
void process_cifar_image(void);
void clear_image_data(void);

uint32_t cifar10_classify(uint8_t image[32][32][3], volatile float *confidence);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/* Retarget printf to ITM for SWO output */
int _write(int file, char *ptr, int len) {
    int i;
    for (i = 0; i < len; i++) {
        ITM_SendChar(*ptr++);
    }
    return len;
}

/* Clear buffer */
void bufclear(void)
{
    memset(buffer, 0, sizeof(buffer));
}

/* Open next batch file */
FRESULT open_next_batch_file(void)
{
    FRESULT fr;

    // Close current file if open
    if (!f_error(&fil)) {
        f_close(&fil);
    }

    // Open the next batch file
    printf("Opening batch file: %s\n", batch_files[current_batch]);
    fr = f_open(&fil, batch_files[current_batch], FA_READ);
    if (fr != FR_OK) {
        printf("Failed to open batch file: %d\n", fr);
    }

    return fr;
}

/* Function to safely extract a number from a string pointer and advance the pointer */
int extract_number(char **ptr)
{
    char *start = *ptr;
    char *end;
    int value;

    // Skip any non-digit characters
    while (*start && (*start < '0' || *start > '9')) {
        start++;
    }

    // Convert to integer
    value = (int)strtol(start, &end, 10);

    // Update the pointer to the end of the number
    *ptr = end;

    return value;
}

/* Read a CIFAR-10 image from the one-line format file - more robust version */
FRESULT read_cifar_oneline_image(void)
{
    FRESULT fr;
    char *ptr, *line_ptr;
    int r, g, b;
    int expected_pixels = 32*32;
    int pixels_read = 0;

    // Clear buffer and image data
    bufclear();
    clear_image_data();

    // Check if file is open
    if (f_error(&fil)) {
        // If no file is open, open the first batch
        fr = open_next_batch_file();
        if (fr != FR_OK) {
            return fr;
        }
    }

    // Read a line from the file
    if (f_gets(buffer, sizeof(buffer), &fil) == NULL) {
        // End of file reached, try next batch
        current_batch++;
        if (current_batch >= NUM_BATCH_FILES) {
            // We've gone through all batches, start over from the first one
            current_batch = 0;
            printf("Completed all batches, starting over from batch 1\n");
        }

        // Open the next batch file
        fr = open_next_batch_file();
        if (fr != FR_OK) {
            return fr;
        }

        // Try reading again from the new file
        if (f_gets(buffer, sizeof(buffer), &fil) == NULL) {
            printf("Error reading from batch file after opening\n");
            return FR_INT_ERR;
        }
    }

    // Parse the line - first check if it has enough content
    if (strlen(buffer) < 100) { // Simple check for minimal valid length
        printf("Line too short to be valid: %lu bytes\n", (unsigned long)strlen(buffer));
        return FR_INT_ERR;
    }

    // Extract IMAGE index and LABEL
    ptr = buffer;

    // Find "IMAGE:" marker
    ptr = strstr(ptr, "IMAGE:");
    if (!ptr) {
        printf("IMAGE: tag not found\n");
        return FR_INT_ERR;
    }
    ptr += 6; // Skip "IMAGE:"

    // Get image index
    current_image_index = extract_number(&ptr);

    // Find "LABEL:" marker
    ptr = strstr(ptr, "LABEL:");
    if (!ptr) {
        printf("LABEL: tag not found\n");
        return FR_INT_ERR;
    }
    ptr += 6; // Skip "LABEL:"

    // Get label
    current_label = (uint8_t)extract_number(&ptr);

    // Find first comma after LABEL
    ptr = strchr(ptr, ',');
    if (!ptr) {
        printf("No comma after LABEL\n");
        return FR_INT_ERR;
    }
    ptr++; // Skip comma

    // Start parsing pixel values - more robust approach
    line_ptr = ptr;
    for (int row = 0; row < 32; row++) {
        for (int col = 0; col < 32; col++) {
            // Check if we've reached the end of the string
            if (!*line_ptr || pixels_read >= expected_pixels) {
                if (pixels_read < expected_pixels) {
                    printf("End of data reached prematurely at pixel %d/%d\n",
                           pixels_read, expected_pixels);
                    return FR_INT_ERR;
                }
                break;
            }

            // Extract R value
            r = extract_number(&line_ptr);
            image_data[row][col][0] = (uint8_t)r;

            // Skip to next value (past comma or any other delimiter)
            while (*line_ptr && *line_ptr != ',' && (*line_ptr < '0' || *line_ptr > '9')) {
                line_ptr++;
            }
            if (*line_ptr == ',') line_ptr++; // Skip comma

            // Check if we've reached the end of the string
            if (!*line_ptr) {
                printf("Unexpected end after R value at pixel %d/%d\n",
                       pixels_read, expected_pixels);
                return FR_INT_ERR;
            }

            // Extract G value
            g = extract_number(&line_ptr);
            image_data[row][col][1] = (uint8_t)g;

            // Skip to next value
            while (*line_ptr && *line_ptr != ',' && (*line_ptr < '0' || *line_ptr > '9')) {
                line_ptr++;
            }
            if (*line_ptr == ',') line_ptr++; // Skip comma

            // Check if we've reached the end of the string
            if (!*line_ptr) {
                printf("Unexpected end after G value at pixel %d/%d\n",
                       pixels_read, expected_pixels);
                return FR_INT_ERR;
            }

            // Extract B value
            b = extract_number(&line_ptr);
            image_data[row][col][2] = (uint8_t)b;

            // Skip to next value
            while (*line_ptr && *line_ptr != ',' && (*line_ptr < '0' || *line_ptr > '9')) {
                line_ptr++;
            }
            if (*line_ptr == ',') line_ptr++; // Skip comma

            // Increment pixel counter
            pixels_read++;
        }
    }

    // Verify we read all pixels
    if (pixels_read < expected_pixels) {
        printf("Not enough pixel data: read %d/%d pixels\n", pixels_read, expected_pixels);
        return FR_INT_ERR;
    }

    // Increment the total images read counter
    total_images_read++;

    // Set the processing flag
    ready_to_process = 1;

    return FR_OK;
}

/* Process the current image - add your image processing code here */
void process_cifar_image(void)
{
    // Example: Calculate average RGB values
    uint32_t avg_r = 0, avg_g = 0, avg_b = 0;

    for (int row = 0; row < 32; row++) {
        for (int col = 0; col < 32; col++) {
            avg_r += image_data[row][col][0];
            avg_g += image_data[row][col][1];
            avg_b += image_data[row][col][2];
        }
    }

    avg_r /= 1024; // 32*32
    avg_g /= 1024;
    avg_b /= 1024;

    printf("Image #%lu (Batch %d): Label=%d, Avg RGB=(%lu,%lu,%lu)\n",
           current_image_index, current_batch + 1, current_label,
           avg_r, avg_g, avg_b);

    // Here you can add your own image processing, such as:
    // - Feature extraction
    // - Classification
    // - Neural network inference
    // - Image transformation

    // Toggle LED to indicate successful processing
    led_state = !led_state;
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_4, led_state ? GPIO_PIN_SET : GPIO_PIN_RESET);

    // Clear the processing flag
    ready_to_process = 0;
}

/* For the memset issue, replace with manual clearing */
void clear_image_data(void)
{
    for (int i = 0; i < 32; i++) {
        for (int j = 0; j < 32; j++) {
            for (int k = 0; k < 3; k++) {
                image_data[i][j][k] = 0;
            }
        }
    }
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_SPI1_Init();
  MX_FATFS_Init();
  /* USER CODE BEGIN 2 */
  /* Delay for SD card initialization */
  HAL_Delay(500);

  printf("\n\n*** CIFAR-10 Full Dataset Reader ***\n\n");

  /* Mount SD Card */
  fresult = f_mount(&fs, "", 1);
  if(fresult != FR_OK) {
      printf("Error in mounting SD Card: %d\n", fresult);

      /* Try to unmount and remount */
      f_mount(NULL, "", 1);
      HAL_Delay(100);
      fresult = f_mount(&fs, "", 1);

      if(fresult != FR_OK) {
          printf("SD Card mount retry failed: %d\n", fresult);
          Error_Handler();
      } else {
          printf("SD Card mount successful after retry!\n");
      }
  } else {
      printf("SD Card mounted successfully!\n");
  }

  /* Check free space */
  fresult = f_getfree("", &fre_clust, &pfs);
  if(fresult == FR_OK) {
      total = (uint32_t)((pfs->n_fatent - 2) * pfs->csize * 0.5);
      printf("SD CARD Total Size: \t%lu KB\n", total);

      free_space = (uint32_t)(fre_clust * pfs->csize * 0.5);
      printf("SD CARD Free Space: \t%lu KB\n", free_space);
  }

  /* List available files to help debugging */
  printf("\nChecking for CIFAR-10 batch files:\n");

  DIR dir;
  FILINFO fno;
  uint8_t batch_files_found = 0;

  fresult = f_opendir(&dir, "/");
  if (fresult == FR_OK) {
      for (;;) {
          fresult = f_readdir(&dir, &fno);
          if (fresult != FR_OK || fno.fname[0] == 0) break;

          // Check if this is a batch file
          for (uint8_t i = 0; i < NUM_BATCH_FILES; i++) {
              if (strcmp(fno.fname, batch_files[i]) == 0) {
                  printf("   [✓] Found batch file: %s (%lu bytes)\n", fno.fname, fno.fsize);
                  batch_files_found++;
                  break;
              }
          }
      }
      f_closedir(&dir);
  }

  printf("\nFound %d of %d batch files\n", batch_files_found, NUM_BATCH_FILES);

  if (batch_files_found == 0) {
      printf("\n*** ERROR: No batch files found! ***\n");
      printf("Please ensure the batch files are in the root directory of the SD card.\n");
      printf("Expected files: cifar10_batch_1.txt through cifar10_batch_10.txt\n");
      Error_Handler();
  }

  /* Initialize variables */
  current_batch = 0;
  total_images_read = 0;

  /* Try to open the first batch file */
  fresult = open_next_batch_file();
  if (fresult != FR_OK) {
      printf("Failed to open first batch file: %d\n", fresult);
      Error_Handler();
  }

  /*
  // Read first image to verify the file format
  printf("\nReading first image to verify format...\n");
  fresult = read_cifar_oneline_image();
  if (fresult != FR_OK) {
      printf("Failed to read first image: %d\n", fresult);

      // Try a different batch file
      printf("Trying a different batch file...\n");
      current_batch = (current_batch + 1) % NUM_BATCH_FILES;
      fresult = open_next_batch_file();
      if (fresult != FR_OK) {
          printf("Failed to open alternate batch file: %d\n", fresult);
          Error_Handler();
      }

      fresult = read_cifar_oneline_image();
      if (fresult != FR_OK) {
          printf("Failed to read from alternate batch file: %d\n", fresult);
          Error_Handler();
      }
  }

  printf("Successfully read first image (Label: %d)\n", current_label);
  printf("First 3 pixels (R,G,B): (%d,%d,%d) (%d,%d,%d) (%d,%d,%d)\n",
         image_data[0][0][0], image_data[0][0][1], image_data[0][0][2],
         image_data[0][1][0], image_data[0][1][1], image_data[0][1][2],
         image_data[0][2][0], image_data[0][2][1], image_data[0][2][2]);

  // Process the first image
  process_cifar_image();
  */

  printf("\nReady to process all 10,000 CIFAR-10 images!\n");
  printf("Starting main loop...\n\n");
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
	    // Start timing
	    uint32_t start_time = HAL_GetTick();

	    // Read the next CIFAR-10 image
	    fresult = read_cifar_oneline_image();

	    if (fresult == FR_OK && ready_to_process) {
	        // Process the image
	        process_cifar_image();

	        /*
	        // Print status every 100 images
	        if (total_images_read % 100 == 0) {
	            printf("\n*** Processed %lu images so far ***\n\n", total_images_read);
	        }
	        */
	    } else if (fresult != FR_OK) {
	        printf("Error reading image: %d\n", fresult);

	        // Try to recover by moving to the next batch file
	        printf("Attempting recovery - switching to next batch file\n");
	        current_batch = (current_batch + 1) % NUM_BATCH_FILES;
	        fresult = open_next_batch_file();
	        if (fresult != FR_OK) {
	            printf("Recovery failed - could not open next batch file\n");
	            HAL_Delay(1000);
	        }
	    }

	    /********** Check the RGB value of image_data => CORRECT **********/
	    /*
	    for (int i = 0; i < 32; i++)
	    {
	    	for (int j = 0; j < 32; j++)
	    	{
	    		printf("(");
	    		for (int k = 0; k < 3; k++)
	    		{
	    			printf("%lu,", image_data[i][j][k]);
	    		}
	    		printf(") ");
	    	}
	    	printf("\n");
	    }
	    */

	    uint32_t predicted_label = cifar10_classify((uint8_t (*)[32][3])image_data, &confidence);

	    // End timing for full iteration
	    uint32_t end_time = HAL_GetTick();
	    uint32_t total_time = end_time - start_time;

	    printf("The model predicts to be label %lu with confidence %.2f%% and inference time %lu ms\n",  predicted_label, confidence * 100.0f, total_time);

	    total_inference_time += total_time;

	    if (predicted_label == current_label)
	    {
	    	printf("TRUE\n");
	    	number_of_true_predicted++;
	    }
	    else
	    	printf("FALSE\n");

	    if (current_image_index == NUMBER_OF_FIRST_IMAGES - 1)
	    {
	    	float accruacy_final = (float)(number_of_true_predicted)/NUMBER_OF_FIRST_IMAGES;
	    	uint32_t average_inference_time = total_inference_time/NUMBER_OF_FIRST_IMAGES;
	    	float total_inference_time_in_sec = (float)(total_inference_time)/1000.0f;

	    	printf("\nFINISHED FIRST %lu IMAGES!\n"
	    	       "- Accruacy after running %lu first images: %.2f%%\n"
	    	       "- Average inference time after running %lu first images: %lu ms\n"
	    	       "- Total inference time after running %lu first images: %.2f (s)\n",
	    	       NUMBER_OF_FIRST_IMAGES, NUMBER_OF_FIRST_IMAGES, accruacy_final * 100.0f,
	    	       NUMBER_OF_FIRST_IMAGES, average_inference_time,
	    	       NUMBER_OF_FIRST_IMAGES, total_inference_time_in_sec);

	        // Add a small delay to ensure all output is transmitted
	        HAL_Delay(1000);

	    	break;
	    }

	    // Small delay to avoid overwhelming the system
	    // Adjust this based on your processing needs
	    HAL_Delay(10);
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 168;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 7;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief SPI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI1_Init(void)
{

  /* USER CODE BEGIN SPI1_Init 0 */

  /* USER CODE END SPI1_Init 0 */

  /* USER CODE BEGIN SPI1_Init 1 */

  /* USER CODE END SPI1_Init 1 */
  /* SPI1 parameter configuration*/
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 10;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI1_Init 2 */

  /* USER CODE END SPI1_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_4, GPIO_PIN_RESET);

  /*Configure GPIO pin : PC4 */
  GPIO_InitStruct.Pin = GPIO_PIN_4;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
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
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
