#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

#ifdef WIN32
    #include <windows.h>
#else
    #include <sys/mman.h>
#endif

#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <pthread.h>
#include "siglent2csv.h"

#define NUM_THREADS 8

#define BILLION 1000000000.0

int input_file = -1;
uint8_t *input_data = NULL;
off_t input_size = -1;
char *output_file_buffer = NULL;
FILE *output_file = NULL;

struct ConversionTask {
    // Beginning and size of the task.
    uint32_t start_index;
    uint32_t length;
    // Pointer to the previous task, used to free the ConversionTask structures at the end.
    struct ConversionTask *previous_task;
    // Pthread thread running this task.
    pthread_t thread;
    // Various parameters read from the file that are necessary for the converter.
    double time_offset;
    double time_scaling_factor;
    uint8_t *ch1_data_offset;
    uint8_t *ch2_data_offset;
    uint8_t *ch3_data_offset;
    uint8_t *ch4_data_offset;
    double ch1_scaling_factor;
    double ch2_scaling_factor;
    double ch3_scaling_factor;
    double ch4_scaling_factor;
    uint8_t csv_line_length;
    const char *format_string;
    char *output_pointer;
    uint8_t enabled_analog_channels;
    int32_t ch1_on;
    int32_t ch2_on;
    int32_t ch3_on;
    int32_t ch4_on;
};

void *conversion_thread(void *ptr) {
    struct ConversionTask *conversion_task = (struct ConversionTask *) ptr;
    uint32_t start_index = conversion_task->start_index;
    uint32_t length = conversion_task->length;
    double time_offset = conversion_task->time_offset;
    double time_scaling_factor = conversion_task->time_scaling_factor;
    uint8_t *ch1_data_offset = conversion_task->ch1_data_offset;
    uint8_t *ch2_data_offset = conversion_task->ch2_data_offset;
    uint8_t *ch3_data_offset = conversion_task->ch3_data_offset;
    uint8_t *ch4_data_offset = conversion_task->ch4_data_offset;
    double ch1_scaling_factor = conversion_task->ch1_scaling_factor;
    double ch2_scaling_factor = conversion_task->ch2_scaling_factor;
    double ch3_scaling_factor = conversion_task->ch3_scaling_factor;
    double ch4_scaling_factor = conversion_task->ch4_scaling_factor;
    uint8_t csv_line_length = conversion_task->csv_line_length;
    const char *format_string = conversion_task->format_string;
    char *output_pointer = conversion_task->output_pointer;
    uint8_t enabled_analog_channels = conversion_task->enabled_analog_channels;
    int32_t ch1_on = conversion_task->ch1_on;
    int32_t ch2_on = conversion_task->ch2_on;
    int32_t ch3_on = conversion_task->ch3_on;
    int32_t ch4_on = conversion_task->ch4_on;

    double channel_values[4];
    double timestamp = time_offset + start_index * time_scaling_factor;

    for (uint32_t i = start_index; i < start_index + length; i++) {
        timestamp += time_scaling_factor;

        uint8_t channel_values_index = 0;
        if (ch1_on) {
            //channel_values[channel_values_index] = (ch1_data_offset[i] - 128) * ch1_scaling_factor + ch1_vert_offset;
            channel_values[channel_values_index] = (ch1_data_offset[i] - 128) * ch1_scaling_factor;
            channel_values_index++;
        }
        if (ch2_on) {
            //channel_values[channel_values_index] = (ch2_data_offset[i] - 128) * ch2_scaling_factor + ch2_vert_offset;
            channel_values[channel_values_index] = (ch2_data_offset[i] - 128) * ch2_scaling_factor;
            channel_values_index++;
        }
        if (ch3_on) {
            //channel_values[channel_values_index] = (ch3_data_offset[i] - 128) * ch3_scaling_factor + ch3_vert_offset;
            channel_values[channel_values_index] = (ch3_data_offset[i] - 128) * ch3_scaling_factor;
            channel_values_index++;
        }
        if (ch4_on) {
            //channel_values[channel_values_index] = (ch4_data_offset[i] - 128) * ch4_scaling_factor + ch4_vert_offset;
            channel_values[channel_values_index] = (ch4_data_offset[i] - 128) * ch4_scaling_factor;
            channel_values_index++;
        }

        if (enabled_analog_channels == 1) {
            snprintf(output_pointer, csv_line_length, format_string, timestamp, channel_values[0]);
        }
        else if (enabled_analog_channels == 2) {
            snprintf(output_pointer, csv_line_length, format_string, timestamp, channel_values[0], channel_values[1]);
        }
        else if (enabled_analog_channels == 3) {
            snprintf(output_pointer, csv_line_length, format_string, timestamp, channel_values[0], channel_values[1], channel_values[2]);
        }
        else if (enabled_analog_channels == 4) {
            snprintf(output_pointer, csv_line_length, format_string, timestamp, channel_values[0], channel_values[1], channel_values[2], channel_values[3]);
        }
        output_pointer[csv_line_length - 1] = '\n';

        output_pointer += csv_line_length;
    }

    return 0;
}

void cleanup() {
    if (input_data) {
        #ifdef WIN32
            UnmapViewOfFile(input_data);
        #else
            munmap(input_data, input_size);
        #endif
        input_data = NULL;
    }
    if (input_file >= 0) {
        close(input_file);
        input_file = -1;
    }
    if (output_file_buffer) {
        free(output_file_buffer);
        output_file_buffer = NULL;
    }
    if (output_file) {
        fclose(output_file);
        output_file = NULL;
    }
}

const char *units_magnitude_prefixes[] = {"y", "z", "a", "f", "p", "n", "u", "m", "", "k", "M", "G", "T", "P"};

const char *units_names[] = {"V", "A", "VV", "AA", "OU", "W", "SQRT_V", "SQRT_A", "INTEGRAL_V", "INTEGRAL_A", "DT_V", "DT_A", "DT_DIV", "Hz", "s", "PTS", "NULL", "dB", "dBV", "dBA", "VPP", "VDC", "dBM"};

double unit_dividers[] = {1.0e24, 1.0e21, 1.0e18, 1.0e15, 1.0e12, 1.0e9, 1.0e6, 1.0e3, 1.0e0, 1.0e-3, 1.0e-6, 1.0e-9, 1.0e-12, 1.0e-15};

const char *unit_magnitude_prefix(uint32_t magnitude) {
    if (magnitude < sizeof(units_magnitude_prefixes) / sizeof(const char *)) {
        return units_magnitude_prefixes[magnitude];
    }
    return "";
}

const char *unit_name(uint32_t unit) {
    if (unit < sizeof(units_names) / sizeof(const char *)) {
        return units_names[unit];
    }
    return "";
}

double unit_divider(uint32_t magnitude) {
    if (magnitude < sizeof(unit_dividers) / sizeof(double)) {
        return unit_dividers[magnitude];
    }
    return 1.0;
}

static_assert(sizeof(double) == 8, "Error: doubles must be 64-bit.");

int main(int argc, char *argv[]) {
    // Parse arguments.
    char *input_filename;
    char *output_filename;
    if (argc == 2) {
        input_filename = argv[1];
        output_filename = "csv_data.csv";
    }
    else if (argc == 3) {
        input_filename = argv[1];
        output_filename = argv[2];
    }
    else {
        fprintf(stderr, "Usage: ./siglent2csv usr_wf_data.bin csv_data.csv\n");
        fprintf(stderr, "    usr_wf_data.bin - .bin file of waveform data downloaded from the \"Waveform Save\" button on the oscilloscope's Web UI.\n");
        fprintf(stderr, "    csv_data.csv - destination filename\n");
        return EXIT_FAILURE;
    }

    // Open input file.
    input_file = open(input_filename, O_RDONLY);
    if (input_file < 0) {
        fprintf(stderr, "Failed to open file %s: %s\n", input_filename, strerror(errno));
        cleanup();
        return EXIT_FAILURE;
    }

    // Get size of input file.
    struct stat input_file_stats;
    if (fstat(input_file, &input_file_stats) < 0) {
        fprintf(stderr, "Failed to stat file %s: %s\n", input_filename, strerror(errno));
        cleanup();
        return EXIT_FAILURE;
    }
    input_size = input_file_stats.st_size;

    // Check size of input file.
    if (input_size < HEADER_SIZE_BYTES) {
        fprintf(stderr, "Input file must be at least %d bytes long.\n", HEADER_SIZE_BYTES);
        cleanup();
        return EXIT_FAILURE;
    }

    // Memory-map input file.
    #ifdef WIN32
        HANDLE file_mapping;
        HANDLE handle = (HANDLE) _get_osfhandle(input_file);
        file_mapping = CreateFileMapping(handle, NULL, PAGE_READONLY, 0, 0, NULL);
        input_data = MapViewOfFile(file_mapping, FILE_MAP_READ, 0, 0, input_size);
    #else
        input_data = mmap(NULL, input_size, PROT_READ, MAP_PRIVATE | MAP_HUGETLB, input_file, 0);
        if (input_data == MAP_FAILED) {
            input_data = mmap(NULL, input_size, PROT_READ, MAP_PRIVATE, input_file, 0);
            if (input_data == MAP_FAILED) {
                fprintf(stderr, "Failed to memory-map file %s: %s\n", input_filename, strerror(errno));
                cleanup();
                return EXIT_FAILURE;
            }
        }
    #endif

    // Parse input_data.
    int32_t ch1_on = *((int32_t *) (input_data + OFFSET_TO_CH1_ON));
    int32_t ch2_on = *((int32_t *) (input_data + OFFSET_TO_CH2_ON));
    int32_t ch3_on = *((int32_t *) (input_data + OFFSET_TO_CH3_ON));
    int32_t ch4_on = *((int32_t *) (input_data + OFFSET_TO_CH4_ON));

    double ch1_volt_div_val = *((double *) (input_data + OFFSET_TO_CH1_VOLT_DIV_VAL));
    //uint32_t ch1_volt_div_val_units = *((uint32_t *) (input_data + OFFSET_TO_CH1_VOLT_DIV_VAL_UNITS));
    uint32_t ch1_volt_div_val_units_magnitude = *((uint32_t *) (input_data + OFFSET_TO_CH1_VOLT_DIV_VAL_UNITS_MAGNITUDE));

    double ch2_volt_div_val = *((double *) (input_data + OFFSET_TO_CH2_VOLT_DIV_VAL));
    //uint32_t ch2_volt_div_val_units = *((uint32_t *) (input_data + OFFSET_TO_CH2_VOLT_DIV_VAL_UNITS));
    uint32_t ch2_volt_div_val_units_magnitude = *((uint32_t *) (input_data + OFFSET_TO_CH2_VOLT_DIV_VAL_UNITS_MAGNITUDE));

    double ch3_volt_div_val = *((double *) (input_data + OFFSET_TO_CH3_VOLT_DIV_VAL));
    //uint32_t ch3_volt_div_val_units = *((uint32_t *) (input_data + OFFSET_TO_CH3_VOLT_DIV_VAL_UNITS));
    uint32_t ch3_volt_div_val_units_magnitude = *((uint32_t *) (input_data + OFFSET_TO_CH3_VOLT_DIV_VAL_UNITS_MAGNITUDE));

    double ch4_volt_div_val = *((double *) (input_data + OFFSET_TO_CH4_VOLT_DIV_VAL));
    //uint32_t ch4_volt_div_val_units = *((uint32_t *) (input_data + OFFSET_TO_CH4_VOLT_DIV_VAL_UNITS));
    uint32_t ch4_volt_div_val_units_magnitude = *((uint32_t *) (input_data + OFFSET_TO_CH4_VOLT_DIV_VAL_UNITS_MAGNITUDE));

    double ch1_vert_offset = *((double *) (input_data + OFFSET_TO_CH1_VERT_OFFSET));
    uint32_t ch1_vert_offset_units = *((uint32_t *) (input_data + OFFSET_TO_CH1_VERT_OFFSET_UNITS));
    uint32_t ch1_vert_offset_units_magnitude = *((uint32_t *) (input_data + OFFSET_TO_CH1_VERT_OFFSET_UNITS_MAGNITUDE));

    double ch2_vert_offset = *((double *) (input_data + OFFSET_TO_CH2_VERT_OFFSET));
    uint32_t ch2_vert_offset_units = *((uint32_t *) (input_data + OFFSET_TO_CH2_VERT_OFFSET_UNITS));
    uint32_t ch2_vert_offset_units_magnitude = *((uint32_t *) (input_data + OFFSET_TO_CH2_VERT_OFFSET_UNITS_MAGNITUDE));

    double ch3_vert_offset = *((double *) (input_data + OFFSET_TO_CH3_VERT_OFFSET));
    uint32_t ch3_vert_offset_units = *((uint32_t *) (input_data + OFFSET_TO_CH3_VERT_OFFSET_UNITS));
    uint32_t ch3_vert_offset_units_magnitude = *((uint32_t *) (input_data + OFFSET_TO_CH3_VERT_OFFSET_UNITS_MAGNITUDE));

    double ch4_vert_offset = *((double *) (input_data + OFFSET_TO_CH4_VERT_OFFSET));
    uint32_t ch4_vert_offset_units = *((uint32_t *) (input_data + OFFSET_TO_CH4_VERT_OFFSET_UNITS));
    uint32_t ch4_vert_offset_units_magnitude = *((uint32_t *) (input_data + OFFSET_TO_CH4_VERT_OFFSET_UNITS_MAGNITUDE));

    /*
    uint32_t digital_on = *((uint32_t *) (input_data + OFFSET_TO_DIGITAL_ON));

    uint32_t d0_on = *((uint32_t *) (input_data + OFFSET_TO_D0_ON));
    uint32_t d1_on = *((uint32_t *) (input_data + OFFSET_TO_D1_ON));
    uint32_t d2_on = *((uint32_t *) (input_data + OFFSET_TO_D2_ON));
    uint32_t d3_on = *((uint32_t *) (input_data + OFFSET_TO_D3_ON));
    uint32_t d4_on = *((uint32_t *) (input_data + OFFSET_TO_D4_ON));
    uint32_t d5_on = *((uint32_t *) (input_data + OFFSET_TO_D5_ON));
    uint32_t d6_on = *((uint32_t *) (input_data + OFFSET_TO_D6_ON));
    uint32_t d7_on = *((uint32_t *) (input_data + OFFSET_TO_D7_ON));
    uint32_t d8_on = *((uint32_t *) (input_data + OFFSET_TO_D8_ON));
    uint32_t d9_on = *((uint32_t *) (input_data + OFFSET_TO_D9_ON));
    uint32_t d10_on = *((uint32_t *) (input_data + OFFSET_TO_D10_ON));
    uint32_t d11_on = *((uint32_t *) (input_data + OFFSET_TO_D11_ON));
    uint32_t d12_on = *((uint32_t *) (input_data + OFFSET_TO_D12_ON));
    uint32_t d13_on = *((uint32_t *) (input_data + OFFSET_TO_D13_ON));
    uint32_t d14_on = *((uint32_t *) (input_data + OFFSET_TO_D14_ON));
    uint32_t d15_on = *((uint32_t *) (input_data + OFFSET_TO_D15_ON));
    */

    double time_div = *((double *) (input_data + OFFSET_TO_TIME_DIV));
    //uint32_t time_div_units = *((uint32_t *) (input_data + OFFSET_TO_TIME_DIV_UNITS));
    //uint32_t time_div_units_magnitude = *((uint32_t *) (input_data + OFFSET_TO_TIME_DIV_UNITS_MAGNITUDE));

    //double time_delay = *((double *) (input_data + OFFSET_TO_TIME_DELAY));
    //uint32_t time_delay_units = *((uint32_t *) (input_data + OFFSET_TO_TIME_DELAY_UNITS));
    //uint32_t time_delay_units_magnitude = *((uint32_t *) (input_data + OFFSET_TO_TIME_DELAY_UNITS_MAGNITUDE));

    uint32_t wave_length = *((uint32_t *) (input_data + OFFSET_TO_WAVE_LENGTH));

    double sample_rate = *((double *) (input_data + OFFSET_TO_SAMPLE_RATE));
    uint32_t sample_rate_units = *((uint32_t *) (input_data + OFFSET_TO_SAMPLE_RATE_UNITS));
    uint32_t sample_rate_units_magnitude = *((uint32_t *) (input_data + OFFSET_TO_SAMPLE_RATE_UNITS_MAGNITUDE));

    //uint32_t digital_wave_length = *((uint32_t *) (input_data + OFFSET_TO_DIGITAL_WAVE_LENGTH));

    //double digital_sample_rate = *((double *) (input_data + OFFSET_TO_DIGITAL_SAMPLE_RATE));
    //uint32_t digital_sample_rate_units = *((uint32_t *) (input_data + OFFSET_TO_DIGITAL_SAMPLE_RATE_UNITS));
    //uint32_t digital_sample_rate_units_magnitude = *((uint32_t *) (input_data + OFFSET_TO_DIGITAL_SAMPLE_RATE_UNITS_MAGNITUDE));

    printf("Sample rate (if no units are shown, defaults to Hertz): %f %s%s\n", sample_rate, unit_magnitude_prefix(sample_rate_units_magnitude), unit_name(sample_rate_units));
    printf("Channels (if no units are shown, defaults to Volts):\n");
    if (ch1_on) {
        printf("CH1 - Vertical offset %f %s%s\n", ch1_vert_offset, unit_magnitude_prefix(ch1_vert_offset_units_magnitude), unit_name(ch1_vert_offset_units));
    }
    if (ch2_on) {
        printf("CH2 - Vertical offset %f %s%s\n", ch2_vert_offset, unit_magnitude_prefix(ch2_vert_offset_units_magnitude), unit_name(ch2_vert_offset_units));
    }
    if (ch3_on) {
        printf("CH3 - Vertical offset %f %s%s\n", ch3_vert_offset, unit_magnitude_prefix(ch3_vert_offset_units_magnitude), unit_name(ch3_vert_offset_units));
    }
    if (ch4_on) {
        printf("CH4 - Vertical offset %f %s%s\n", ch4_vert_offset, unit_magnitude_prefix(ch4_vert_offset_units_magnitude), unit_name(ch4_vert_offset_units));
    }

    uint8_t *ch1_data_offset;
    uint8_t *ch2_data_offset;
    uint8_t *ch3_data_offset;
    uint8_t *ch4_data_offset;
    uint8_t *data_offset_counter = input_data + OFFSET_TO_ANALOG_DATA;
    uint8_t enabled_analog_channels = 0;
    if (ch1_on) {
        ch1_data_offset = data_offset_counter;
        data_offset_counter += wave_length;
        enabled_analog_channels++;
    }
    if (ch2_on) {
        ch2_data_offset = data_offset_counter;
        data_offset_counter += wave_length;
        enabled_analog_channels++;
    }
    if (ch3_on) {
        ch3_data_offset = data_offset_counter;
        data_offset_counter += wave_length;
        enabled_analog_channels++;
    }
    if (ch4_on) {
        ch4_data_offset = data_offset_counter;
        data_offset_counter += wave_length;
        enabled_analog_channels++;
    }

    const char *format_string;
    uint8_t csv_line_length;
    if (enabled_analog_channels == 0) {
        fprintf(stderr, "Error: No analog channels detected in file.\n");
        cleanup();
        return EXIT_FAILURE;
    }
    else if (enabled_analog_channels == 1) {
        format_string = "% .11f,% 6f";
        csv_line_length = 27;
    }
    else if (enabled_analog_channels == 2) {
        format_string = "% .11f,% 6f,% 6f";
        csv_line_length = 35;
    }
    else if (enabled_analog_channels == 3) {
        format_string = "% .11f,% 6f,% 6f,% 6f";
        csv_line_length = 43;
    }
    else if (enabled_analog_channels == 4) {
        format_string = "% .11f,% 6f,% 6f,% 6f,% 6f";
        csv_line_length = 51;
    }

    double ch1_scaling_factor = ch1_volt_div_val / unit_divider(ch1_volt_div_val_units_magnitude) / CODE_PER_DIV;
    double ch2_scaling_factor = ch2_volt_div_val / unit_divider(ch2_volt_div_val_units_magnitude) / CODE_PER_DIV;
    double ch3_scaling_factor = ch3_volt_div_val / unit_divider(ch3_volt_div_val_units_magnitude) / CODE_PER_DIV;
    double ch4_scaling_factor = ch4_volt_div_val / unit_divider(ch4_volt_div_val_units_magnitude) / CODE_PER_DIV;

    double time_offset = -(time_div * 14.0 / 2.0);
    double time_scaling_factor = (1.0 / sample_rate);

    output_file_buffer = malloc(wave_length * csv_line_length + 1); // + 1 is for the \0 terminator on the end of the last snprintf() output.
    size_t output_file_buffer_length = wave_length * csv_line_length;
    char *output_pointer = output_file_buffer;

    //===========================================================================
    /*
    clock_t start = clock();
    double sum = 0.0;
    for (uint32_t i = 0; i < wave_length; i++) {
        sum += (ch1_data_offset[i] - 128) * ch1_scaling_factor;
    }
    clock_t end = clock();
    printf("Sum: %f\n", sum);
    printf("Average: %f\n", sum / (double) wave_length);
    double cpu_time_used = ((double) (end - start)) / CLOCKS_PER_SEC;
    printf("Summing all values for CH1 took %f seconds.\n", cpu_time_used);
    */
    //===========================================================================

    struct timespec start, end;
    clock_gettime(CLOCK_REALTIME, &start);
    uint32_t maximum_task_size = wave_length / NUM_THREADS;
    uint32_t start_index = 0;
    uint32_t task_size = 0;
    struct ConversionTask *previous_task = NULL;
    while (start_index < wave_length) {
        // Calculate the size of this conversion task.
        if (start_index + maximum_task_size < wave_length) {
            task_size = maximum_task_size; 
        }
        else {
            task_size = wave_length - start_index;
        }

        // Set parameters of the conversion task.
        struct ConversionTask *conversion_task = calloc(1, sizeof(struct ConversionTask));
        conversion_task->start_index = start_index;
        conversion_task->length = task_size;
        conversion_task->time_offset = time_offset;
        conversion_task->time_scaling_factor = time_scaling_factor;
        conversion_task->ch1_data_offset = ch1_data_offset;
        conversion_task->ch2_data_offset = ch2_data_offset;
        conversion_task->ch3_data_offset = ch3_data_offset;
        conversion_task->ch4_data_offset = ch4_data_offset;
        conversion_task->ch1_scaling_factor = ch1_scaling_factor;
        conversion_task->ch2_scaling_factor = ch2_scaling_factor;
        conversion_task->ch3_scaling_factor = ch3_scaling_factor;
        conversion_task->ch4_scaling_factor = ch4_scaling_factor;
        conversion_task->csv_line_length = csv_line_length;
        conversion_task->format_string = format_string;
        conversion_task->output_pointer = output_pointer + start_index * csv_line_length;
        conversion_task->enabled_analog_channels = enabled_analog_channels;
        conversion_task->ch1_on = ch1_on;
        conversion_task->ch2_on = ch2_on;
        conversion_task->ch3_on = ch3_on;
        conversion_task->ch4_on = ch4_on;
        conversion_task->previous_task = previous_task;

        // Start conversion thread.
        pthread_create(&conversion_task->thread, NULL, conversion_thread, (void *) conversion_task);

        // Get ready to create next conversion task.
        previous_task = conversion_task;
        start_index += maximum_task_size;
    }
    // Wait for conversion threads to finish.
    for (struct ConversionTask *task = previous_task; task; task = task->previous_task) {
        pthread_join(task->thread, NULL);
    }
    clock_gettime(CLOCK_REALTIME, &end);
    double time_used = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / BILLION;
    printf("CSV data export took %f seconds.\n", time_used);

    clock_gettime(CLOCK_REALTIME, &start);
    output_file = fopen(output_filename, "w");
    if (!output_file) {
        fprintf(stderr, "Failed to open file %s for writing: %s\n", output_filename, strerror(errno));
        cleanup();
        return EXIT_FAILURE;
    }
    if (fwrite(output_file_buffer, 1, output_file_buffer_length, output_file) != output_file_buffer_length) {
        fprintf(stderr, "Failed to write to file %s.\n", output_filename);
        cleanup();
        return EXIT_FAILURE;
    }
    clock_gettime(CLOCK_REALTIME, &end);
    time_used = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / BILLION;
    printf("CSV data write took %f seconds.\n", time_used);

    clock_gettime(CLOCK_REALTIME, &start);
    struct ConversionTask *task = previous_task;
    while (task != NULL) {
        struct ConversionTask *temp = task;
        task = task->previous_task;
        free(temp);
    }
    cleanup();
    clock_gettime(CLOCK_REALTIME, &end);
    time_used = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / BILLION;
    printf("Resource cleanup took %f seconds.\n", time_used);

    return 0;
}
