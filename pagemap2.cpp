//#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <vector>
#include <sys/types.h> 
#define PAGE_SIZE 0x1000

#define FIND_LIB_NAME

unsigned long long largestRegion = 0;
unsigned long long curRegion = 1;
float averageRegion = 0;
unsigned long long iteration = 0;
unsigned long long previousPFN = 0; 
unsigned long long continuousVP = 0;
unsigned long long numVP = 0;
std::vector <unsigned long long> region_vector;
// [0]  - 1
// [1]  - 2~3
// [2]  - 4~7
// [3]  - 8~15
// [4]  - 16~31
// [5]  - 32~63
// [6]  - 64~127
// [7]  - 128~255
// [8]  - 256~511
// [9]  - 512~1023
// [10] - 1024~

static void print_page(unsigned long long address, unsigned long long data,
    const char *lib_name) {

    printf("0x%-16llx : pfn %-16llx soft-dirty %d file/shared %d "
        "swapped %d present %d library %s\n",
        address,
        data & 0x7fffffffffffff,
        (data >> 55) & 1,
        (data >> 61) & 1,
        (data >> 62) & 1,
        (data >> 63) & 1,
        lib_name);

    unsigned long long pfn = data & 0x7fffffffffffff;
    if(pfn != 0) {

            if((pfn == (previousPFN+1)) || (pfn == (previousPFN-1))|| (pfn == (previousPFN))) {
                continuousVP++;
                curRegion++;
            }
            else  {
              if(curRegion > largestRegion)  {
                largestRegion = curRegion;
              }
              averageRegion = (float) (iteration * averageRegion + curRegion) / (float) (iteration+1);
              iteration++;
              
              if(curRegion == 1)  {
                region_vector[0]++;
              } else if( 2 <= curRegion < 4) {
                region_vector[1]++;
              } else if( 4 <= curRegion < 8) {
                region_vector[2]++;
              } else if( 8 <= curRegion < 16) {
                region_vector[3]++;
              } else if( 16 <= curRegion < 32) {
                region_vector[4]++;
              } else if( 33 <= curRegion < 64) {
                region_vector[5]++;
              } else if( 64 <= curRegion < 128) {
                region_vector[6]++;
              } else if( 128 <= curRegion < 256) {
                region_vector[7]++;
              } else if( 256 <= curRegion < 512) {
                region_vector[8]++;
              } else if( 512 <= curRegion < 1024) {
                region_vector[9]++;
              } else if( 1024 <= curRegion) {
                region_vector[10]++;
              }
              curRegion = 1;
            }
            previousPFN = pfn;
            numVP++;
            float contRatio = (float) continuousVP / (float) numVP;
            printf("continuousVP: %llu\t totalVP: %llu\t continuous ratio: %f\ncurrent region: %llu\t average region: %f\tlargest region: %llu\n", continuousVP, numVP, contRatio, curRegion, averageRegion, largestRegion);
            printf("region[VPs==1]        : %llu\n", region_vector[0]);
            printf("region[VPs==2~3]      : %llu\n", region_vector[1]);
            printf("region[VPs==4~7]      : %llu\n", region_vector[2]);
            printf("region[VPs==8~15]     : %llu\n", region_vector[3]);
            printf("region[VPs==16~31]    : %llu\n", region_vector[4]);
            printf("region[VPs==32~63]    : %llu\n", region_vector[5]);
            printf("region[VPs==64~127]   : %llu\n", region_vector[6]);
            printf("region[VPs==128~255]  : %llu\n", region_vector[7]);
            printf("region[VPs==256~511]  : %llu\n", region_vector[8]);
            printf("region[VPs==512~1023] : %llu\n", region_vector[9]);
            printf("region[VPs==1024~   ] : %llu\n", region_vector[10]);
    }
}

void handle_virtual_range(int pagemap, unsigned long long start_address,
    unsigned long long end_address, const char *lib_name) {

    for(unsigned long long i = start_address; i < end_address; i += 0x1000) {
        unsigned long long data;
        unsigned long long index = (i / PAGE_SIZE) * sizeof(data);
        if(pread(pagemap, &data, sizeof(data), index) != sizeof(data)) {
            if(errno) perror("pread");
            break;
        }

        print_page(i, data, lib_name);
    }
}

void parse_maps(const char *maps_file, const char *pagemap_file) {
    int maps = open(maps_file, O_RDONLY);
    if(maps < 0) return;

    int pagemap = open(pagemap_file, O_RDONLY);
    if(pagemap < 0) {
        close(maps);
        return;
    }

    char buffer[BUFSIZ];
    int offset = 0;

    for(;;) {
        ssize_t length = read(maps, buffer + offset, sizeof buffer - offset);
        if(length <= 0) break;

        length += offset;

        for(size_t i = offset; i < (size_t)length; i ++) {
            unsigned long long low = 0, high = 0;
            if(buffer[i] == '\n' && i) {
                size_t x = i - 1;
                while(x && buffer[x] != '\n') x --;
                if(buffer[x] == '\n') x ++;
                size_t beginning = x;

                while(buffer[x] != '-' && x < sizeof buffer) {
                    char c = buffer[x ++];
                    low *= 16;
                    if(c >= '0' && c <= '9') {
                        low += c - '0';
                    }
                    else if(c >= 'a' && c <= 'f') {
                        low += c - 'a' + 10;
                    }
                    else break;
                }

                while(buffer[x] != '-' && x < sizeof buffer) x ++;
                if(buffer[x] == '-') x ++;

                while(buffer[x] != ' ' && x < sizeof buffer) {
                    char c = buffer[x ++];
                    high *= 16;
                    if(c >= '0' && c <= '9') {
                        high += c - '0';
                    }
                    else if(c >= 'a' && c <= 'f') {
                        high += c - 'a' + 10;
                    }
                    else break;
                }

                const char *lib_name = 0;
#ifdef FIND_LIB_NAME
                for(int field = 0; field < 4; field ++) {
                    x ++;  // skip space
                    while(buffer[x] != ' ' && x < sizeof buffer) x ++;
                }
                while(buffer[x] == ' ' && x < sizeof buffer) x ++;

                size_t y = x;
                while(buffer[y] != '\n' && y < sizeof buffer) y ++;
                buffer[y] = 0;

                lib_name = buffer + x;
#endif

                handle_virtual_range(pagemap, low, high, lib_name);

#ifdef FIND_LIB_NAME
                buffer[y] = '\n';
#endif
            }
        }
    }

    close(maps);
    close(pagemap);
}


void process_pid(pid_t pid) {
    char maps_file[BUFSIZ];
    char pagemap_file[BUFSIZ];
    snprintf(maps_file, sizeof(maps_file),
        "/proc/%lu/maps", (unsigned long)pid);
    snprintf(pagemap_file, sizeof(pagemap_file),
        "/proc/%lu/pagemap", (unsigned long)pid);

    parse_maps(maps_file, pagemap_file);
}

int main(int argc, char *argv[]) {
    if(argc < 2) {
        printf("Usage: %s pid1 [pid2...]\n", argv[0]);
        return 1;
    }

    for(unsigned long long j = 0; j < 11; j++)  {
      region_vector.push_back(0);
    }

    for(int i = 1; i < argc; i ++) {
        pid_t pid = (pid_t)strtoul(argv[i], NULL, 0);

        printf("=== Maps for pid %d\n", (int)pid);
        process_pid(pid);
    }

    return 0;
}

