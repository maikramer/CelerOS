//
// Created by maikeu on 18/08/2019.
//

#ifndef SINGLETON_H
#define SINGLETON_H

#include "esp_log.h"

template<typename T>
class Singleton {
public:

    Singleton(const Singleton &) = delete;

    Singleton &operator=(const Singleton) = delete;

    static T &instance() {
        static T instance{token{}};
        return instance;
    }

protected:
    struct token {
    };

    Singleton() = default;
};

#endif //SINGLETON_H
