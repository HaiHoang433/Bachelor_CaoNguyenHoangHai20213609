#include <stdint.h>
#include <math.h>
#include "cnn_params.h"

// Function to classify a CIFAR-10 image
uint32_t cifar10_classify(uint8_t image[32][32][3], volatile float *confidence) {
    // Create a non-volatile copy of the image data for processing
    uint8_t local_image[32][32][3];

    // Copy from volatile to non-volatile
    for (int h = 0; h < 32; h++) {
        for (int w = 0; w < 32; w++) {
            for (int c = 0; c < 3; c++) {
                local_image[h][w][c] = image[h][w][c];
            }
        }
    }

    // Define intermediate buffers for each layer
    // Input grayscale after Lambda layer: 32x32x1
    float grayscale[32][32];

    // First conv layer output: 32x32x4
    float conv0_output[32][32][CONV0_OUT_CHANNELS];

    // After first pooling: 16x16x4
    float pool1_output[16][16][CONV0_OUT_CHANNELS];

    // Second conv layer output: 16x16x8
    float conv1_output[16][16][CONV1_OUT_CHANNELS];

    // After second pooling: 8x8x8
    float pool3_output[8][8][CONV1_OUT_CHANNELS];

    // Final layer outputs
    float dense_output[DENSE_OUTPUT_SIZE];

    // Step 1: Convert RGB to Grayscale (Lambda layer)
    for (int h = 0; h < 32; h++) {
        for (int w = 0; w < 32; w++) {
            // Average the RGB channels
            grayscale[h][w] = (local_image[h][w][0] + local_image[h][w][1] + local_image[h][w][2]) / 3.0f / 255.0f;
        }
    }

    // Step 2: First Convolution Layer
    for (int h = 0; h < 32; h++) {
        for (int w = 0; w < 32; w++) {
            for (int f = 0; f < CONV0_OUT_CHANNELS; f++) {
                float sum = conv0_biases[f];

                // Apply 3x3 kernel
                for (int kh = 0; kh < CONV0_KERNEL_SIZE; kh++) {
                    for (int kw = 0; kw < CONV0_KERNEL_SIZE; kw++) {
                        int h_idx = h - 1 + kh;
                        int w_idx = w - 1 + kw;

                        // Zero padding
                        if (h_idx >= 0 && h_idx < 32 && w_idx >= 0 && w_idx < 32) {
                            sum += grayscale[h_idx][w_idx] * conv0_weights[f][0][kh][kw];
                        }
                    }
                }

                // ReLU activation
                conv0_output[h][w][f] = (sum > 0) ? sum : 0;
            }
        }
    }

    // Step 3: First Max Pooling Layer (2x2)
    for (int h = 0; h < 16; h++) {
        for (int w = 0; w < 16; w++) {
            for (int c = 0; c < CONV0_OUT_CHANNELS; c++) {
                // Find max in 2x2 region
                float max_val = 0;
                for (int ph = 0; ph < POOL1_SIZE; ph++) {
                    for (int pw = 0; pw < POOL1_SIZE; pw++) {
                        float val = conv0_output[h*2 + ph][w*2 + pw][c];
                        if ((ph == 0 && pw == 0) || val > max_val) {
                            max_val = val;
                        }
                    }
                }
                pool1_output[h][w][c] = max_val;
            }
        }
    }

    // Step 4: Second Convolution Layer
    for (int h = 0; h < 16; h++) {
        for (int w = 0; w < 16; w++) {
            for (int f = 0; f < CONV1_OUT_CHANNELS; f++) {
                float sum = conv1_biases[f];

                // Apply 3x3 kernel
                for (int kh = 0; kh < CONV1_KERNEL_SIZE; kh++) {
                    for (int kw = 0; kw < CONV1_KERNEL_SIZE; kw++) {
                        for (int c = 0; c < CONV1_IN_CHANNELS; c++) {
                            int h_idx = h - 1 + kh;
                            int w_idx = w - 1 + kw;

                            // Zero padding
                            if (h_idx >= 0 && h_idx < 16 && w_idx >= 0 && w_idx < 16) {
                                sum += pool1_output[h_idx][w_idx][c] * conv1_weights[f][c][kh][kw];
                            }
                        }
                    }
                }

                // ReLU activation
                conv1_output[h][w][f] = (sum > 0) ? sum : 0;
            }
        }
    }

    // Step 5: Second Max Pooling Layer (2x2)
    for (int h = 0; h < 8; h++) {
        for (int w = 0; w < 8; w++) {
            for (int c = 0; c < CONV1_OUT_CHANNELS; c++) {
                // Find max in 2x2 region
                float max_val = 0;
                for (int ph = 0; ph < POOL3_SIZE; ph++) {
                    for (int pw = 0; pw < POOL3_SIZE; pw++) {
                        float val = conv1_output[h*2 + ph][w*2 + pw][c];
                        if ((ph == 0 && pw == 0) || val > max_val) {
                            max_val = val;
                        }
                    }
                }
                pool3_output[h][w][c] = max_val;
            }
        }
    }

    // Step 6: Flatten the output (8x8x8 = 512 values)
    float flattened[DENSE_INPUT_SIZE];
    int idx = 0;
    for (int h = 0; h < 8; h++) {
        for (int w = 0; w < 8; w++) {
            for (int c = 0; c < CONV1_OUT_CHANNELS; c++) {
                flattened[idx++] = pool3_output[h][w][c];
            }
        }
    }

    // Step 7: Dense layer
    for (int i = 0; i < DENSE_OUTPUT_SIZE; i++) {
        float sum = dense_biases[i];
        for (int j = 0; j < DENSE_INPUT_SIZE; j++) {
            sum += flattened[j] * dense_weights[j][i];
        }
        dense_output[i] = sum;
    }

    // Step 8: Softmax activation
    float max_val = dense_output[0];
    for (int i = 1; i < DENSE_OUTPUT_SIZE; i++) {
        if (dense_output[i] > max_val) {
            max_val = dense_output[i];
        }
    }

    float sum_exp = 0.0f;
    for (int i = 0; i < DENSE_OUTPUT_SIZE; i++) {
        dense_output[i] = expf(dense_output[i] - max_val);
        sum_exp += dense_output[i];
    }

    for (int i = 0; i < DENSE_OUTPUT_SIZE; i++) {
        dense_output[i] /= sum_exp;
    }

    // Step 9: Find the class with the highest probability
    uint32_t predicted_class = 0;
    float max_prob = dense_output[0];
    for (int i = 1; i < DENSE_OUTPUT_SIZE; i++) {
        if (dense_output[i] > max_prob) {
            max_prob = dense_output[i];
            predicted_class = i;
        }
    }

    // Return confidence value
    *confidence = max_prob;

    return predicted_class;
}
