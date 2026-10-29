// The normal recursive hand test target runs these pure vectors too. The same
// source is independently compiled by validation/l6/check_completion.py without
// linking SDK/Runtime/I/O; no shared CMake registration is needed.
#define L6_COMPLETION_TAGS "[comm][protocol][l6][completion]"
#include "../../protocol/l6/test_completion_vectors.cpp"
