#include <iostream>

#ifdef _WIN32
__declspec(dllexport)
#else
__attribute__ ((visibility ("default")))
#endif
void exportedFunction()
{
    std::cout << "exportedFunction(): Change me!\n";
}

namespace
{
void hiddenFunction()
{
    std::cout << "hiddenFunction(): Change me!\n";
}
}

int main()
{
    std::cout << "main(): Change me!\n";
    hiddenFunction();
    exportedFunction();
    return 0;
}

namespace
{
class Test
{
    public:
        Test()
        {
            std::cout << "Test::Test(): Change me!\n";
        }
};
Test test;
}
