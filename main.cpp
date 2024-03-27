#include "out/damloader.pb.h"
#include "xz-embedded/linux/include/linux/xz.h"
#include <bits/stdint-uintn.h>
#include <bzlib.h>
#include <cstdio>
#include <dirent.h>
#include <endian.h>
#include <fcntl.h>
#include <fstream>
#include <google/protobuf/repeated_field.h>
#include <iostream>
#include <ostream>
#include <sstream>
#include <stdint.h>
#include <sys/dir.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
using namespace chromeos_update_engine;
constexpr uint64_t kDeltaManifestSizeOffset =
    12 /* (kDeltaVersionOffset + kDeltaVersionSize) */;
constexpr uint64_t kDeltaManifestSizeSize = 8;
constexpr uint64_t kDeltaMetadataSignatureSizeSize = 4;

constexpr uint64_t kDeltaManifestOffset = kDeltaManifestSizeOffset +
                                          kDeltaManifestSizeSize +
                                          kDeltaMetadataSignatureSizeSize;
constexpr const char *TrueString = "TRUE";
constexpr const char *FalseString = "FALSE";
const uint64_t kSparseHole = std::numeric_limits<uint64_t>::max();
constexpr size_t kOutBufferSize = 16 * 1024;
std::string Truthy(bool condition) {
  return condition ? TrueString : FalseString;
}
bool IsInstallOperationReplace(int i) {
  return i == chromeos_update_engine::InstallOperation::REPLACE_XZ ||
         i == chromeos_update_engine::InstallOperation::REPLACE ||
         i == chromeos_update_engine::InstallOperation::REPLACE_BZ;
}
static size_t extent_bytes_written = 0;
static google::protobuf::RepeatedPtrField<Extent>::iterator current_extent_;
void write_data_op(FILE *f, char *buf, InstallOperation *op, size_t block_size,
                   size_t opdatalength) {
  uint64_t bytes_written = 0;
  size_t new_size = opdatalength;

  while (bytes_written < opdatalength) {
    if (current_extent_ == op->dst_extents().end()) {
      printf("Something went wrong and we don't know why during the extent "
             "writing process!\n");
      return;
    }
    size_t bytes_to_write_remain =
        current_extent_->num_blocks() * block_size - extent_bytes_written;

    size_t bytes_to_write =
        std::min(new_size - bytes_to_write, bytes_to_write_remain);

    if (current_extent_->start_block() != kSparseHole) {
      const off64_t offset =
          current_extent_->start_block() * block_size + extent_bytes_written;
      fseek(f, offset, SEEK_SET);
      printf("Wrote %lu bytes at %lu offset of kernel.bin!\n", bytes_to_write,
             offset);
      fwrite(buf + bytes_written, bytes_to_write, 1, f);
    } else {
      printf("Skipped %lu bytes\n", bytes_to_write);
    }
    bytes_written += bytes_to_write;
    extent_bytes_written += bytes_to_write;
    if (extent_bytes_written == bytes_to_write) {
      extent_bytes_written = 0;
      current_extent_++;
    }
  }
}
void print_usage(const char *programName) {
  printf("%s <signed update payload> <reconstructed kernel bits>\n",
         programName);
  printf("If you need help getting access to the signed payload, visit "
         "https://www.github.com/jay0lee/chromeos-update-directory/\n");
  printf("Tool made by Writable!\n");
}
int main(int argc, char const *argv[]) {

  chromeos_update_engine::DeltaArchiveManifest dam;
  if (argc < 2) {
    const char *progName = argc > 0 ? argv[0] : "";
    print_usage(progName);
    return -1;
  }
  const char *fileName = argv[1];
  size_t block_size = 0;
  char *buffer = (char *)malloc(24);
  uint64_t deltaManifestSize = 0;
  FILE *file = fopen(fileName, "r");
  fread(buffer, 24, 1, file);

  memcpy(&deltaManifestSize, &buffer[kDeltaManifestSizeOffset],
         kDeltaManifestSizeSize);
  deltaManifestSize = be64toh(deltaManifestSize);
  fseek(file, kDeltaManifestOffset, SEEK_SET);
  char *damBuf = new char[deltaManifestSize];
  fread(damBuf, deltaManifestSize, 1, file);

  printf("Received manifest size of %lu\n", deltaManifestSize);
  std::string damString;
  damString.append(damBuf, deltaManifestSize);
  dam.ParseFromString(damString);
  block_size = dam.block_size();
  printf("Block size: %lu\n", block_size);
  int partitionNum = dam.partitions().size();
  printf("Received %d partitions\n", partitionNum);
  bool isContinuous = false;

  for (int i = 0; i < partitionNum; i++) {
    chromeos_update_engine::PartitionUpdate pu = dam.partitions().at(i);
    int off = pu.partition_name().find("kernel");
    bool iskernel = off >= 0;
    printf("Partition name: %s\n", pu.partition_name().c_str());
    printf("Is kernel: %s\n", (iskernel ? "No" : "Yes"));
    printf("Partition Operation count: %d\n", pu.operations().size());
    int puOpNum = pu.operations().size();
    int prevOpOffset = 0;
    int prevOpSize = 0;
    FILE *f = nullptr;
    if (iskernel) {

      printf("Reconstructing kernel on size %lu\n",
             pu.new_partition_info().size());
      f = fopen("kernel.bin", "w+");
      {
        std::vector<char> empty(dam.block_size(), 0);
        for (size_t s = 0; s < pu.new_partition_info().size();
             s += dam.block_size()) {
          fwrite(empty.data(), 4096, 1, f);
        }
        // Vector will be collected after the scope exits.
      }
    }

    for (int j = 0; j < puOpNum; j++) {
      chromeos_update_engine::InstallOperation op = pu.operations().at(j);
      // Make sure this is populated for later partition.
      if (!prevOpOffset) {
        prevOpOffset = op.data_offset();
      }
      isContinuous = prevOpOffset + prevOpSize == op.data_offset();
      prevOpSize = op.data_length();

      printf("Operation %d\n", j);
      bool isReplaceOperation = IsInstallOperationReplace(op.type());
      printf("\tReplace Operation?: %s\n", Truthy(isReplaceOperation).c_str());
      if (isReplaceOperation) {
        printf("\tReplace Operation is xz: %s\n",
               Truthy(op.type() ==
                      chromeos_update_engine::InstallOperation::REPLACE_XZ)
                   .c_str());
        printf("\tReplace Operation is bz: %s\n",
               Truthy(op.type() ==
                      chromeos_update_engine::InstallOperation::REPLACE_BZ)
                   .c_str());
        printf("\tReplace Operation is regular: %s\n",
               Truthy(op.type() ==
                      chromeos_update_engine::InstallOperation::REPLACE)
                   .c_str());
      }
      printf("\tOperation offset (if any): %lu\n", op.data_offset());
      printf("\tOperation size (if any): %lu\n", op.data_length());
      uint64_t extent_bytes_written = 0;

      if (iskernel) {
        printf("Reconstructing kernel!\n");
        char *buf = new char[op.data_length()];
        auto extents = op.dst_extents().begin();
        fseek(file, op.data_offset(), SEEK_SET);
        fread(buf, op.data_length(), 1, file);
        size_t new_size = op.data_length();
        extent_bytes_written = 0;
        std::vector<uint8_t> out_buffer(kOutBufferSize);

        if (op.type() == InstallOperation::REPLACE_XZ) {
          xz_dec *d = xz_dec_init(XZ_DYNALLOC, 64 * 1024 * 1024);
          xz_buf bufx;
          bufx.in = reinterpret_cast<const uint8_t *>(buf);
          bufx.in_pos = 0;
          bufx.in_size = op.data_length();
          bufx.out = out_buffer.data();
          bufx.out_size = kOutBufferSize;
          bufx.out_pos = 0;
          printf("Inflating xz!\n");
          for (;;) {
            bufx.out_pos = 0;
            xz_ret ret = xz_dec_run(d, &bufx);
            if (bufx.out_pos == 0) {
              break;
            }
            write_data_op(f, (char *)out_buffer.data(), &op, block_size,
                          bufx.out_pos);
            if (ret == XZ_STREAM_END) {
              printf("XZ states that stream ended!\n");
            }
            if (bufx.in_size == bufx.in_pos) {
              printf("XZ stream ended!\n");
              break;
            }
          }

        } else if (op.type() == InstallOperation::REPLACE_BZ) {
          bz_stream bs;
          BZ2_bzDecompressInit(&bs, 0, 0);
          bs.next_in = (char *)buf;
          bs.avail_in = op.data_length();
          printf("Inflating BZ2 data!\n");
          for (;;) {
            bs.next_out = (char *)out_buffer.data();
            bs.avail_out = kOutBufferSize;
            int rc = BZ2_bzDecompress(&bs);
            if (!(rc == BZ_OK || rc == BZ_STREAM_END)) {
              printf("An error has occurred during the extraction of bzip "
                     "data.\n");
              return -1;
            }
            write_data_op(f, (char*)out_buffer.data(), &op, block_size, out_buffer.size() - bs.avail_out);

            if (bs.avail_in == 0) {
              break; // No more data to process.
            }
          }
        } else {
          write_data_op(f, buf, &op, block_size, op.data_length());
        }


        uint64_t bytes_written = 0;
        uint64_t extent_bytes_written = 0;
      }
    }
    if (f) {
      fclose(f);
    }

    if (isContinuous) {
      printf("Operation is continuous!!!\n");
    }
  }

  fclose(file);

  return 0;
}
