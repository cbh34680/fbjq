// executor/main.cpp
#include <iostream>
#include <thread>
#include <chrono>

int main()
{
	std::cout << "ENTER: executor" << std::endl;
	std::this_thread::sleep_for(std::chrono::seconds(10));
	std::cout << "LEAVE: executor" << std::endl;
	
	return 0;
}
