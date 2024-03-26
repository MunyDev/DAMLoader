#include "out/damloader.pb.h"
#include <bits/stdint-uintn.h>
#include <endian.h>
#include <iostream>
#include <fstream>
#include <ostream>
#include <sstream>
constexpr uint64_t kDeltaManifestSizeOffset = 12 /* (kDeltaVersionOffset + kDeltaVersionSize) */;
constexpr uint64_t kDeltaManifestSizeSize = 8;
constexpr uint64_t kDeltaMetadataSignatureSizeSize = 4;

constexpr uint64_t kDeltaManifestOffset = kDeltaManifestSizeOffset + kDeltaManifestSizeSize + kDeltaMetadataSignatureSizeSize;

void print_usage(const char* programName) {
    printf("%s <signed update payload>\n", programName);
    printf("If you need help getting access to the signed payload, visit https://www.github.com/jay0lee/chromeos-update-directory/\n");
    printf("DAMLoader made by Writable!\n");
}
int main(int argc, char const *argv[])
{

    chromeos_update_engine::DeltaArchiveManifest dam;
    if (argc < 2) {
        const char* progName = argc > 0 ? argv[0] : "";
        print_usage(progName);
        return -1;
    }
    const char* fileName = argv[1];
    char* buffer = (char*)malloc(24);
    uint64_t deltaManifestSize = 0;
    FILE* file = fopen(fileName, "r");
    fread(buffer, 24, 1, file);

    memcpy(&deltaManifestSize, &buffer[kDeltaManifestSizeOffset], kDeltaManifestSizeSize);
    deltaManifestSize = be64toh(deltaManifestSize);

    fseek(file, kDeltaManifestOffset, SEEK_SET);
    char* damBuf = new char[deltaManifestSize];
    fread(damBuf, deltaManifestSize, 1, file);
    
    printf("Received manifest size of %lu\n", deltaManifestSize);
    dam.ParseFromString(damBuf);
    int partitionNum = dam.partitions().size();
    printf("Received %d partitions\n", partitionNum);
    for (int i = 0; i < partitionNum; i++){
        chromeos_update_engine::PartitionUpdate pu = dam.partitions().at(i);
        int off = pu.partition_name().find("KERNEL");
        printf("Partition name: %s\n", pu.partition_name().c_str());
        printf("Is kernel: %s\n", (off < 0 ? "Yes" : "No"));
    }

    fclose(file);
    
    return 0;
}
