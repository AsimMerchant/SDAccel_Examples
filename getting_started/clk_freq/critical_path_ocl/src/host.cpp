/**********
Copyright (c) 2018, Xilinx, Inc.
All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
this list of conditions and the following disclaimer in the documentation
and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its contributors
may be used to endorse or promote products derived from this software
without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**********/
#include "xcl2.hpp"
#include <vector>
#include "bitmap.h"

int main(int argc, char* argv[])
{
    if (argc != 4)
    {
        std::cout << "Usage: " << argv[0] 
            << " <apply_watermark_GOOD/apply_watermark_BAD> " 
            << " <input bitmap> <golden bitmap>" << std::endl;
        return EXIT_FAILURE ;
    }
    std::string binaryName = argv[1];
    std::string bitmapFilename = argv[2];
    std::string goldenFilename = argv[3];
  
    //Read the input bit map file into memory
    BitmapInterface image(bitmapFilename.data());
    bool result = image.readBitmapFile() ;
    if (!result)
    {
        std::cout << "ERROR:Unable to Read Input Bitmap File "
            << bitmapFilename.data() << std::endl;
        return EXIT_FAILURE ;
    }

    //Read the golden bit map file into memory
    BitmapInterface goldenImage(goldenFilename.data());
    result = goldenImage.readBitmapFile() ;
    if (!result)
    {
        std::cout << "ERROR:Unable to Read Golden Bitmap File "
            << goldenFilename.data() << std::endl;
        return EXIT_FAILURE ;
    }

    int width = image.getWidth() ;
    int height = image.getHeight() ;
   
    //Allocate Memory in Host Memory 
    size_t image_size_bytes = image.numPixels() * sizeof(int); 
    std::vector<int,aligned_allocator<int>> inImage(image.numPixels());
    std::vector<int,aligned_allocator<int>> outImage(image.numPixels());

    //Copying image host buffer
    memcpy(inImage.data(),image.bitmap(),image_size_bytes);

//OPENCL HOST CODE AREA START

    int err;
    unsigned fileBufSize;
    std::vector<cl::Device> devices = xcl::get_xil_devices();
    cl::Device device = devices[0];

    OCL_CHECK(err, cl::Context context(device, NULL, NULL, NULL, &err));
    OCL_CHECK(err, cl::CommandQueue q(context, device, CL_QUEUE_PROFILING_ENABLE, &err));
    OCL_CHECK(err, std::string device_name = device.getInfo<CL_DEVICE_NAME>(&err)); 

    std::string binaryFile = xcl::find_binary_file(device_name,binaryName.c_str());
    char* fileBuf = xcl::read_binary_file(binaryFile, fileBufSize);
    cl::Program::Binaries bins{{fileBuf, fileBufSize}};
    devices.resize(1);
    OCL_CHECK(err, cl::Program program(context, devices, bins, NULL, &err));
    OCL_CHECK(err, cl::Kernel kernel(program,"apply_watermark", &err));

    std::vector<cl::Memory> inBufVec, outBufVec;
    OCL_CHECK(err, cl::Buffer buffer_inImage(context, CL_MEM_READ_ONLY | CL_MEM_USE_HOST_PTR, 
            image_size_bytes, inImage.data(), &err));
    OCL_CHECK(err, cl::Buffer buffer_outImage(context,CL_MEM_USE_HOST_PTR | CL_MEM_WRITE_ONLY, 
            image_size_bytes, outImage.data(), &err));
    inBufVec.push_back(buffer_inImage);
    outBufVec.push_back(buffer_outImage);

    //Copy input Image to device global memory
    OCL_CHECK(err, err = q.enqueueMigrateMemObjects(inBufVec,0/* 0 means from host*/));
  
    auto krnl_applyWatermark= cl::KernelFunctor<cl::Buffer&, cl::Buffer& ,int,int>(kernel);
    
    //Launch the Kernel
    krnl_applyWatermark(cl::EnqueueArgs(q,cl::NDRange(1,1,1), cl::NDRange(1,1,1)), 
            buffer_inImage, buffer_outImage, width, height);

    //Copy Result from Device Global Memory to Host Local Memory
    OCL_CHECK(err, err = q.enqueueMigrateMemObjects(outBufVec,CL_MIGRATE_MEM_OBJECT_HOST));
    OCL_CHECK(err, err = q.finish());
//OPENCL HOST CODE AREA END


    //Compare Golden Image with Output image
    bool match = 0;
    if ( image.getHeight() != goldenImage.getHeight() || image.getWidth() != goldenImage.getWidth()){
        match = 1;
    }else{
        int* goldImgPtr = goldenImage.bitmap();
        for (unsigned int i = 0 ; i < image.numPixels(); i++){
            if (outImage[i] != goldImgPtr[i]){
                match = 1;
                printf ("Pixel %d Mismatch Output %x and Expected %x \n", i, outImage[i], goldImgPtr[i]);
                break;
            }
        }
    }
    
    // Write the final image to disk
    image.writeBitmapFile(outImage.data());
    delete[] fileBuf;

    std::cout << "TEST " << (match ? "FAILED" : "PASSED") << std::endl; 
    return (match ? EXIT_FAILURE :  EXIT_SUCCESS);
}
