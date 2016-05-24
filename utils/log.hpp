#include <iostream>

// not thread-safety.
// use at your own risk.
#define DEBUG(s) OUTPUT(debug, s)
#define ERROR(s) OUTPUT(error, s)
#define OUTPUT(level, s) std::cout << "[" #level "] " << s << std::endl
