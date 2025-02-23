# Purpose:
This branch aims to provide support for the cnpy library on Windows platform.

NumPy offers the `save` method for easy saving of arrays into .npy and `savez` for zipping multiple .npy arrays together into a .npz file. 

`cnpy` lets you read and write to these formats in C++. 

The motivation comes from scientific programming where large amounts of data are generated in C++ and analyzed in Python.

Writing to .npy has the advantage of using low-level C++ I/O (fread and fwrite) for speed and binary format for size. 
The .npy file header takes care of specifying the size, shape, and data type of the array, so specifying the format of the data is unnecessary.

Loading data written in numpy formats into C++ is equally simple, but requires you to type-cast the loaded data to the type of your choice.

# Using:

In CMakeLists.txt:

```bash
include(FetchContent)
# Set the third-party directory where external libraries will be stored
set(THIRD_PARTY_DIR ${CMAKE_SOURCE_DIR}/third_party)
# Define the cnpy repository
FetchContent_Declare(
    cnpy
    GIT_REPOSITORY https://github.com/juelinl/cnpy.git
    SOURCE_DIR ${THIRD_PARTY_DIR}/cnpy
    GIT_TAG windows
)
FetchContent_MakeAvailable(cnpy)
```

Then link to your target:
```bash
target_link_libraries(YOUR_TARGET PRIVATE cnpy)
```

In your code:
```cpp
#include <cnpy.h>
```

See [example](example/main.cpp) for more detail.

# Description:

There are two functions for writing data: `npy_save` and `npz_save`.

There are 3 functions for reading:
- `npy_load` will load a .npy file. 
- `npz_load(fname)` will load a .npz and return a dictionary of NpyArray structues. 
- `npz_load(fname,varname)` will load and return the NpyArray for data varname from the specified .npz file.

The data structure for loaded data is below. 
Data is accessed via the `data<T>()`-method, which returns a pointer of the specified type (which must match the underlying datatype of the data). 
The array shape and word size are read from the npy header.

```c++
struct NpyArray {
    std::vector<size_t> shape;
    size_t word_size;
    template<typename T> T* data();
};
```