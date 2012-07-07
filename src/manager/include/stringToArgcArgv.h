#pragma once
#ifndef STRINGTOARGCARGV_H
#define STRINGTOARGCARGV_H

std::vector<std::string> parse(const std::string& args);
void stringToArgcArgv(const std::string& str, int* argc, char*** argv);

#endif
