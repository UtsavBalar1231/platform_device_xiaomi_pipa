#include <android/log.h>
#include <android/sensor.h>
#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>

const char kPackageName[] = "xiaomi-keyboard";

#define BUFFER_SIZE 1024

// Device path
#define NANODEV_PATH "/dev/nanodev0"
#define EVENT_PATH "/dev/input/event12"

// logging
#define TAG "xiaomi-keyboard"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR,    TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,     TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,     TAG, __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG,    TAG, __VA_ARGS__)

// Pre-calculated sin and cos tables to optimize code
static const float angle_cos[] = {1.0f, 0.999848f, 0.999391f, 0.99863f, 0.997564f, 0.996195f, 0.994522f, 0.992546f, 0.990268f, 0.987688f, 0.984808f, 0.981627f, 0.978148f, 0.97437f, 0.970296f, 0.965926f, 0.961262f, 0.956305f, 0.951057f, 0.945519f, 0.939693f, 0.93358f, 0.927184f, 0.920505f, 0.913545f, 0.906308f, 0.898794f, 0.891007f, 0.882948f, 0.87462f, 0.866025f, 0.857167f, 0.848048f, 0.838671f, 0.829038f, 0.819152f, 0.809017f, 0.798635f, 0.788011f, 0.777146f, 0.766044f, 0.75471f, 0.743145f, 0.731354f, 0.71934f, 0.707107f, 0.694658f, 0.681998f, 0.669131f, 0.656059f, 0.642788f, 0.62932f, 0.615661f, 0.601815f, 0.587785f, 0.573576f, 0.559193f, 0.544639f, 0.529919f, 0.515038f, 0.5f, 0.48481f, 0.469472f, 0.453991f, 0.438371f, 0.422618f, 0.406737f, 0.390731f, 0.374607f, 0.358368f, 0.34202f, 0.325568f, 0.309017f, 0.292372f, 0.275637f, 0.258819f, 0.241922f, 0.224951f, 0.207912f, 0.190809f, 0.173648f, 0.156434f, 0.139173f, 0.121869f, 0.104528f, 0.087156f, 0.069756f, 0.052336f, 0.034899f, 0.017452f, -0.0f};
static const float angle_sin[] = {0.0f, 0.017452f, 0.034899f, 0.052336f, 0.069756f, 0.087156f, 0.104528f, 0.121869f, 0.139173f, 0.156434f, 0.173648f, 0.190809f, 0.207912f, 0.224951f, 0.241922f, 0.258819f, 0.275637f, 0.292372f, 0.309017f, 0.325568f, 0.34202f, 0.358368f, 0.374607f, 0.390731f, 0.406737f, 0.422618f, 0.438371f, 0.453991f, 0.469472f, 0.48481f, 0.5f, 0.515038f, 0.529919f, 0.544639f, 0.559193f, 0.573576f, 0.587785f, 0.601815f, 0.615662f, 0.62932f, 0.642788f, 0.656059f, 0.669131f, 0.681998f, 0.694658f, 0.707107f, 0.71934f, 0.731354f, 0.743145f, 0.75471f, 0.766044f, 0.777146f, 0.788011f, 0.798636f, 0.809017f, 0.819152f, 0.829038f, 0.838671f, 0.848048f, 0.857167f, 0.866025f, 0.87462f, 0.882948f, 0.891007f, 0.898794f, 0.906308f, 0.913545f, 0.920505f, 0.927184f, 0.93358f, 0.939693f, 0.945519f, 0.951057f, 0.956305f, 0.961262f, 0.965926f, 0.970296f, 0.97437f, 0.978148f, 0.981627f, 0.984808f, 0.987688f, 0.990268f, 0.992546f, 0.994522f, 0.996195f, 0.997564f, 0.99863f, 0.999391f, 0.999848f, 1.0f};

// Nanodev file
int fd;

// Global variables to hold pad the accelerometer data
float padX = 0.0f;
float padY = 0.0f;
float padZ = 0.0f;

// Global variables to hold kb the accelerometer data
float kbX = 0.0f;
float kbY = 0.0f;
float kbZ = 0.0f;

// Current kb enabled/disabled state
bool kb_status = true;

// Condition variable for pausing and resuming the thread
pthread_mutex_t acc_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t acc_cond = PTHREAD_COND_INITIALIZER;
bool acc_paused = false;

// Sensor variables
const ASensor* sensor;
ASensorEventQueue* queue;

// Initialize the sensors module
int init_sensors() {
	ASensorManager* sensor_manager =
					ASensorManager_getInstanceForPackage(kPackageName);
	if (!sensor_manager) {
		LOGE("Failed to get a sensor manager");
		return 1;
	}

	queue = ASensorManager_createEventQueue(
									sensor_manager,
									ALooper_prepare(ALOOPER_PREPARE_ALLOW_NON_CALLBACKS),
									1, NULL /* no callback */, NULL /* no data */);
	if (!queue) {
		LOGE("Failed to create a sensor event queue");
		return 1;
	}

	sensor = ASensorManager_getDefaultSensor(sensor_manager,
															ASENSOR_TYPE_ACCELEROMETER);

	return 0;
}

// Read acc data
int read_acc() {
	if (sensor && !ASensorEventQueue_enableSensor(queue, sensor)) {
		ASensorEvent data;
		usleep(150000);
		if (ASensorEventQueue_getEvents(queue, &data, 1)) {
			padX = data.acceleration.x;
			padY = data.acceleration.y;
			padZ = data.acceleration.z;
		} else
			return -1;

		int ret = ASensorEventQueue_disableSensor(queue, sensor);
		if (ret) {
			LOGW("Failed to disable acc: %d", ret);
			return ret;
		}
	} else
		return -1;
	return 0;
}

float invSqrt(float num) {
	float xHalf = 0.5f * num;
	int i = *(int*)&num;
	i = 0x5f3759df - (i >> 1);
	float y = *(float*)&i;
	y = y * (1.5f - (xHalf * y * y)); // Newton's method step
	return y;
}

int calculateAngle(float kX, float kY, float kZ, float pX, float pY, float pZ) {
	float deviation2;
	float deviation1;
	float recipNorm = invSqrt((kX * kX) + (kY * kY) + (kZ * kZ));
	float kX2 = kX * recipNorm;
	float kY2 = kY * recipNorm;
	float kZ2 = kZ * recipNorm;
	float recipNorm2 = invSqrt((pX * pX) + (pY * pY) + (pZ * pZ));
	float pX2 = pX * recipNorm2;
	float pY2 = pY * recipNorm2;
	float pZ2 = pZ * recipNorm2;
	float min_deviation = 100.0f;
	int min_deviation_angle = 0;
	for (int i = 0; i <= 360; i++) {
		if (i > 90) {
			if (i > 90 && i <= 180) {
			   float deviation12 = (((-angle_cos[180 - i]) * kX2) - (angle_sin[180 - i] * kZ2)) + pX2;
				deviation2 = ((-angle_cos[180 - i]) * kZ2) + (angle_sin[180 - i] * kX2) + pZ2;
				deviation1 = deviation12;
			} else if (i > 180 && i <= 270) {
				float deviation13 = (((-angle_cos[i - 180]) * kX2) - ((-angle_sin[i - 180]) * kZ2)) + pX2;
				deviation2 = ((-angle_cos[i - 180]) * kZ2) + ((-angle_sin[i - 180]) * kX2) + pZ2;
				deviation1 = deviation13;
			} else {
				float deviation14 = ((angle_cos[360 - i] * kX2) - ((-angle_sin[360 - i]) * kZ2)) + pX2;
				deviation2 = (angle_cos[360 - i] * kZ2) + ((-angle_sin[360 - i]) * kX2) + pZ2;
				deviation1 = deviation14;
			}
		} else {
			float f = angle_cos[i];
			float f2 = angle_sin[i];
			float deviation15 = ((f * kX2) - (f2 * kZ2)) + pX2;
			deviation2 = (f * kZ2) + (f2 * kX2) + pZ2;
			deviation1 = deviation15;
		}
		if (abs(deviation1) + abs(deviation2) < min_deviation) {
			float min_deviation2 = abs(deviation1) + abs(deviation2);
			min_deviation_angle = i;
			min_deviation = min_deviation2;
		}
	}
	float accel_angle_error = abs(pY2) > abs(kY2) ? abs(pY2) : abs(kY2);
	if (accel_angle_error > 0.98f) {
		return -1;
	}
    printf("Angle: %d, status: %d\n", min_deviation_angle, kb_status);
	return min_deviation_angle;
}

void set_kb_state(bool value, bool force) {
    printf("set_kb called, new value: %d\n", value);
	if (kb_status == value && !force)
		return;
	kb_status = value;
        printf("set_kb called, setting new value: %d\n", value);
	unsigned char buf[3] = {0x32, 0xFF, value};
	write(fd, &buf, 3);
}

void acc_handle(char *buffer){
	int x = ((buffer[7] << 4) & 4080) | ((buffer[6] >> 4) & 15);
	int y = ((buffer[9] << 4) & 4080) | ((buffer[8] >> 4) & 15);
	int z = ((buffer[11] << 4) & 4080) | ((buffer[10] >> 4) & 15);

	if ((x & 2048) == 2048) {
		x = -(4096 - x);
	}

	if ((y & 2048) == 2048) {
		y = -(4096 - y);
	}

	if ((z & 2048) == 2048) {
		z = -(4096 - z);
	}

	float x_normal = (x * 9.8f) / 256.0f;
	float y_normal = ((-y) * 9.8f) / 256.0f;
	float z_normal = ((-z) * 9.8f) / 256.0f;

	// Read tablet acc
	if (read_acc()){
		LOGE("Failed to read acc");
		return;
	}

	kbX = x_normal;
	kbY = y_normal;
	kbZ = z_normal;

	int angle = calculateAngle(x_normal, y_normal, z_normal, padX, padY, padZ);
	set_kb_state(!(angle > 230 || angle < 40), false);
}

void* acc_thread_func(void* arg) {
	while (1) {
		// Check if the event file exists
		if (access(EVENT_PATH, F_OK) != -1 || !kb_status) {
			pthread_mutex_lock(&acc_mutex);
			while (acc_paused) {
				// Wait for the condition signal to resume
				pthread_cond_wait(&acc_cond, &acc_mutex);
			}
			pthread_mutex_unlock(&acc_mutex);
			// Read accelerometer data
			if (read_acc() == 0) {
				// Calculate the angle
				int angle = calculateAngle(kbX, kbY, kbZ, padX, padY, padZ);
				set_kb_state(!(angle > 230 || angle < 40), false);
			}
		} else {
			usleep(3000000);
		}
	}
	return NULL;
}

int main() {
	ssize_t bytes_read;
	char buffer[BUFFER_SIZE];

	if (init_sensors() != 0) {
		LOGE("Failed to initialize sensors");
		return EXIT_FAILURE;
	}

	// Open the device file for reading
	fd = open(NANODEV_PATH, O_RDWR);
	if (fd == -1) {
		LOGE("Error opening device");
		return errno;
	}

	// Check current kb status
	if (access(EVENT_PATH, F_OK) == -1)
        kb_status = false;

	// Create the accelerometer thread
	pthread_t acc_thread;
	if (pthread_create(&acc_thread, NULL, acc_thread_func, NULL) != 0) {
		LOGE("Failed to create accelerometer thread");
		return EXIT_FAILURE;
	}

	// Main loop
	while (1) {
		// Read data from the device
		bytes_read = read(fd, buffer, BUFFER_SIZE);
		if (bytes_read > 0) {
			if(buffer[0]!=34 && buffer[0]!=35 && buffer[0]!=36 && buffer[0]!=38)
				continue;

			if(buffer[1]==0x31 && buffer[2]==0x38){
				if(buffer[4]==100){
					acc_handle(buffer);
				} else if(buffer[4]==37 || buffer[4]==40) {
					if(buffer[5]!=1)
						continue;
					if(buffer[6]==1) {
						LOGI("Got sleap event: unsleap");
						pthread_mutex_lock(&acc_mutex);
						acc_paused = false;
						pthread_cond_signal(&acc_cond); // Signal the condition variable to wake up the thread
						pthread_mutex_unlock(&acc_mutex);

						// Restor input device state
						set_kb_state(kb_status, true);
					} else {
						LOGI("Got sleap event: sleap");
						pthread_mutex_lock(&acc_mutex);
						acc_paused = true;
						pthread_mutex_unlock(&acc_mutex);
					}
				}
			}
		} else if (bytes_read == 0) {
			// No data, you might want to sleep for a bit
			sleep(1);
		} else {
			// An error occurred
			LOGE("Error reading device");
			break;
		}
	}

	// Join the thread when done
	pthread_join(acc_thread, NULL);


	// Close the device file
	close(fd);
	return 0;
}
