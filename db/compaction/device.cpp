#include "db/compaction/device.h"

namespace ROCKSDB_NAMESPACE {
void init_device(cl::Context &context, cl::CommandQueue &q,
                 cl::Kernel &krnl_compact, const std::string &binaryFileName,
                 cl_int &err) {
  auto devices = xcl::get_xil_devices();
  // read_binary_file() is a utility API which will load the binaryFile
  // and will return the pointer to file buffer.
  auto fileBuf = xcl::read_binary_file(binaryFileName);
  cl::Program::Binaries bins{{fileBuf.data(), fileBuf.size()}};
  bool valid_device = false;
  for (unsigned int i = 0; i < devices.size(); i++) {
    auto device = devices[i];
    // Creating Context and Command Queue for selected Device
    OCL_CHECK(err,
              context = cl::Context(device, nullptr, nullptr, nullptr, &err));
    OCL_CHECK(err, q = cl::CommandQueue(context, device,
                                        CL_QUEUE_PROFILING_ENABLE, &err));
    std::cout << "Trying to program device[" << i
              << "]: " << device.getInfo<CL_DEVICE_NAME>() << std::endl;
    cl::Program program(context, {device}, bins, nullptr, &err);
    if (err != CL_SUCCESS) {
      std::cout << "Failed to program device[" << i << "] with xclbin file!\n";
    } else {
      std::cout << "Device[" << i << "]: program successful!\n";
      OCL_CHECK(err, krnl_compact = cl::Kernel(program, "compaction", &err));
      valid_device = true;
      break;  // we break because we found a valid device
    }
  }
  if (!valid_device) {
    std::cout << "Failed to program any device found, exit!\n";
    exit(EXIT_FAILURE);
  }
}

cl_mem_ext_ptr_t get_buffer_extension(int ddr_no) {
  cl_mem_ext_ptr_t ext;
  switch (ddr_no) {
    case 0:
      ext.flags = XCL_MEM_DDR_BANK0;
      break;
    case 1:
      ext.flags = XCL_MEM_DDR_BANK1;
      break;
    case 2:
      ext.flags = XCL_MEM_DDR_BANK2;
      break;
    case 3:
      ext.flags = XCL_MEM_DDR_BANK3;
  };
  return ext;
}

}  // namespace ROCKSDB_NAMESPACE
