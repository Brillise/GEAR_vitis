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

// An event callback function that prints the operations performed by the OpenCL
// runtime.
void event_cb(cl_event event1, cl_int cmd_status, void *data) {
  cl_int err;
  cl_command_type command;
  cl::Event event(event1, true);
  OCL_CHECK(err, err = event.getInfo(CL_EVENT_COMMAND_TYPE, &command));
  cl_int status;
  OCL_CHECK(err,
            err = event.getInfo(CL_EVENT_COMMAND_EXECUTION_STATUS, &status));
  const char *command_str;
  const char *status_str;
  switch (command) {
    case CL_COMMAND_READ_BUFFER:
      command_str = "buffer read";
      break;
    case CL_COMMAND_WRITE_BUFFER:
      command_str = "buffer write";
      break;
    case CL_COMMAND_NDRANGE_KERNEL:
      command_str = "kernel";
      break;
    case CL_COMMAND_MAP_BUFFER:
      command_str = "kernel";
      break;
    case CL_COMMAND_COPY_BUFFER:
      command_str = "kernel";
      break;
    case CL_COMMAND_MIGRATE_MEM_OBJECTS:
      command_str = "buffer migrate";
      break;
    default:
      command_str = "unknown";
  }
  switch (status) {
    case CL_QUEUED:
      status_str = "Queued";
      break;
    case CL_SUBMITTED:
      status_str = "Submitted";
      break;
    case CL_RUNNING:
      status_str = "Executing";
      break;
    case CL_COMPLETE:
      status_str = "Completed";
      break;
  }
  // printf("[%s]: %s %s\n", reinterpret_cast<char *>(data), status_str,
  // command_str); fflush(stdout);
}

void set_callback(cl::Event event, const char *queue_name) {
  cl_int err;
  OCL_CHECK(err,
            err = event.setCallback(CL_COMPLETE, event_cb, (void *)queue_name));
}

}  // namespace ROCKSDB_NAMESPACE
