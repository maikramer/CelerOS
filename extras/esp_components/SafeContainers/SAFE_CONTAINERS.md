# SafeContainers Component

Biblioteca de containers thread-safe para ESP32/FreeRTOS usando primitivas de sincronização do FreeRTOS.

## Índice

- [Visão Geral](#visão-geral)
- [ScopedLock](#scopedlock)
- [SafeList](#safelist)
- [SafeMap](#safemap)
- [SafeQueue](#safequeue)
- [SafeVector](#safevector)
- [NativeQueue](#nativequeue)
- [Padrões de Uso](#padrões-de-uso)

---

## Visão Geral

SafeContainers fornece wrappers thread-safe para containers STL comuns, usando mutex do FreeRTOS para sincronização.

### Características

- **RAII-based locking**: `ScopedLock` e `IterationGuard` previnem deadlocks
- **API moderna C++17**: `std::optional`, `std::function`, move semantics
- **Type-safe**: Templates para verificação em tempo de compilação
- **ISR-safe**: `NativeQueue` suporta operações em contexto de interrupção

### Namespace

```cpp
namespace SafeContainers {
    class ScopedLock;
    class RecursiveScopedLock;
    class LockableBase;
    template<typename T> class SafeList;
    template<typename TKey, typename TValue> class SafeMap;
    template<typename T> class SafeQueue;
    template<typename T> class SafeVector;
    template<typename T> class NativeQueue;
}
```

---

## ScopedLock

RAII wrapper para mutex FreeRTOS que automaticamente libera o lock na destruição.

### API

```cpp
class ScopedLock {
public:
    explicit ScopedLock(SemaphoreHandle_t mutex, TickType_t timeout = pdMS_TO_TICKS(1000));
    ~ScopedLock();  // Auto-unlock
    
    bool isLocked() const;
    explicit operator bool() const;
    void unlock();
    bool tryLock(TickType_t timeout = pdMS_TO_TICKS(1000));
};
```

### Exemplo

```cpp
SemaphoreHandle_t mutex = xSemaphoreCreateMutex();

void safeOperation() {
    ScopedLock lock(mutex);
    if (lock) {
        // Operação protegida pelo mutex
        sharedResource.modify();
    }
    // Mutex automaticamente liberado aqui
}
```

---

## SafeList

Lista duplamente ligada thread-safe baseada em `std::list`.

### API Principal

```cpp
template<typename T>
class SafeList {
public:
    // Operações básicas
    bool empty() const;
    size_type size() const;
    void clear();
    
    // Acesso a elementos
    std::optional<T> front() const;
    std::optional<T> back() const;
    
    // Modificadores
    bool pushFront(const T& value);
    bool pushBack(const T& value);
    std::optional<T> popFront();
    std::optional<T> popBack();
    bool removeIf(std::function<bool(const T&)> predicate);
    size_type removeAllIf(std::function<bool(const T&)> predicate);
    
    // Busca
    bool contains(const T& value) const;
    std::optional<T> find(std::function<bool(const T&)> predicate) const;
    
    // Iteração segura
    void forEach(std::function<void(const T&)> func) const;
    void forEachMut(std::function<void(T&)> func);
    IterationGuard iterationGuard(uint32_t timeoutMs = 1000);
    bool withLock(std::function<void(std::list<T>&)> func);
    
    // Ordenação
    void sort(std::function<bool(const T&, const T&)> compare);
    void sort();  // Usa operator<
    
    // Bulk
    std::vector<T> toVector() const;
    template<typename InputIt>
    bool insertRange(InputIt begin, InputIt end);
};
```

### Exemplo

```cpp
using namespace SafeContainers;

SafeList<std::string> messages;

// Adicionar elementos
messages.pushBack("Hello");
messages.pushBack("World");

// Iterar de forma segura
messages.forEach([](const std::string& msg) {
    ESP_LOGI("MSG", "%s", msg.c_str());
});

// Buscar elemento
if (auto found = messages.find([](const std::string& s) { return s == "Hello"; })) {
    ESP_LOGI("FOUND", "Encontrado: %s", found->c_str());
}

// Remover condicionalmente
messages.removeIf([](const std::string& s) { return s.empty(); });

// Iteração manual com guard
{
    auto guard = messages.iterationGuard();
    if (guard) {
        for (const auto& msg : guard) {
            // Processar mensagem
        }
    }
} // Lock liberado automaticamente
```

---

## SafeMap

Mapa associativo thread-safe baseado em `std::map`.

### API Principal

```cpp
template<typename TKey, typename TValue>
class SafeMap {
public:
    // Operações básicas
    bool empty() const;
    size_type size() const;
    void clear();
    
    // Acesso
    bool contains(const TKey& key) const;
    std::optional<TValue> get(const TKey& key) const;
    TValue getOrDefault(const TKey& key, const TValue& defaultValue) const;
    std::pair<bool, TValue> operator[](const TKey& key) const;
    
    // Modificadores
    bool insert(const TKey& key, const TValue& value);
    bool insertIfAbsent(const TKey& key, const TValue& value);
    bool update(const TKey& key, const TValue& value);
    bool erase(const TKey& key);
    std::optional<TValue> extract(const TKey& key);
    
    // Iteração segura
    void forEach(std::function<void(const TKey&, const TValue&)> func) const;
    void forEachMut(std::function<void(const TKey&, TValue&)> func);
    IterationGuard iterationGuard(uint32_t timeoutMs = 1000);
    bool withLock(std::function<void(std::map<TKey, TValue>&)> func);
    
    // Bulk
    std::vector<TKey> keys() const;
    std::vector<TValue> values() const;
    std::vector<std::pair<TKey, TValue>> toVector() const;
};
```

### Exemplo

```cpp
using namespace SafeContainers;

SafeMap<std::string, int> scores;

// Inserir valores
scores.insert("player1", 100);
scores.insert("player2", 85);

// Obter valor
if (auto score = scores.get("player1")) {
    ESP_LOGI("SCORE", "Player1: %d", *score);
}

// Valor com default
int p3Score = scores.getOrDefault("player3", 0);

// Atualizar se existir
scores.update("player1", 150);

// Iterar
scores.forEach([](const std::string& name, int score) {
    ESP_LOGI("SCORES", "%s: %d", name.c_str(), score);
});

// Obter e remover
if (auto removed = scores.extract("player2")) {
    ESP_LOGI("REMOVED", "Score removido: %d", *removed);
}
```

---

## SafeQueue

Fila FIFO thread-safe baseada em `std::queue`.

### API Principal

```cpp
template<typename T>
class SafeQueue {
public:
    SafeQueue();
    explicit SafeQueue(size_type maxSize);  // Com limite de tamanho
    
    // Operações básicas
    bool empty() const;
    size_type size() const;
    bool full() const;
    void clear();
    
    // Acesso (sem remover)
    std::optional<T> front() const;
    std::optional<T> back() const;
    
    // Modificadores
    bool push(const T& value);
    bool push(T&& value);
    std::optional<T> pop();
    std::optional<T> pushOverwrite(const T& value);  // Descarta mais antigo se cheio
    
    // Bulk
    std::vector<T> popAll();
    std::vector<T> popN(size_type maxCount);
    void forEach(std::function<void(const T&)> func) const;
    size_type processWhile(std::function<bool(T&)> func);
    
    // Configuração
    void setMaxSize(size_type maxSize);
    size_type maxSize() const;
};
```

### Exemplo

```cpp
using namespace SafeContainers;

// Fila com limite de 100 elementos
SafeQueue<Message> messageQueue(100);

// Produtor
void producer() {
    Message msg{"Hello", 42};
    if (!messageQueue.push(msg)) {
        ESP_LOGW("PROD", "Fila cheia!");
    }
}

// Consumidor
void consumer() {
    while (auto msg = messageQueue.pop()) {
        processMessage(*msg);
    }
}

// Processar em lote
auto batch = messageQueue.popN(10);
for (const auto& msg : batch) {
    processMessage(msg);
}

// Limpar fila antiga
auto discarded = messageQueue.popAll();
ESP_LOGI("QUEUE", "Descartados: %d mensagens", discarded.size());
```

---

## SafeVector

Array dinâmico thread-safe baseado em `std::vector`.

### API Principal

```cpp
template<typename T>
class SafeVector {
public:
    SafeVector();
    explicit SafeVector(size_type capacity);
    
    // Operações básicas
    bool empty() const;
    size_type size() const;
    size_type capacity() const;
    void clear();
    void reserve(size_type n);
    void shrinkToFit();
    
    // Acesso
    std::optional<T> at(size_type index) const;
    T atOrDefault(size_type index, const T& defaultValue) const;
    std::optional<T> front() const;
    std::optional<T> back() const;
    
    // Modificadores
    bool pushBack(const T& value);
    std::optional<T> popBack();
    bool set(size_type index, const T& value);
    bool insert(size_type index, const T& value);
    std::optional<T> erase(size_type index);
    bool removeIf(std::function<bool(const T&)> predicate);
    void resize(size_type newSize, const T& value = T{});
    
    // Busca
    bool contains(const T& value) const;
    std::optional<size_type> indexOf(const T& value) const;
    std::optional<T> find(std::function<bool(const T&)> predicate) const;
    size_type count(std::function<bool(const T&)> predicate) const;
    
    // Iteração segura
    void forEach(std::function<void(const T&)> func) const;
    void forEachMut(std::function<void(T&)> func);
    void forEachIndexed(std::function<void(size_type, const T&)> func) const;
    IterationGuard iterationGuard(uint32_t timeoutMs = 1000);
    
    // Ordenação
    void sort(std::function<bool(const T&, const T&)> compare);
    void sort();
    
    // Bulk/Transformação
    std::vector<T> copy() const;
    template<typename U>
    std::vector<U> transform(std::function<U(const T&)> func) const;
    std::vector<T> filter(std::function<bool(const T&)> predicate) const;
};
```

### Exemplo

```cpp
using namespace SafeContainers;

SafeVector<Sensor> sensors(10);  // Reserva inicial

// Adicionar sensores
sensors.emplaceBack("Temperature", 25.0f);
sensors.emplaceBack("Humidity", 60.0f);

// Acessar por índice
if (auto sensor = sensors.at(0)) {
    ESP_LOGI("SENSOR", "Valor: %.2f", sensor->value);
}

// Modificar elemento
sensors.forEachMut([](Sensor& s) {
    s.value = readSensor(s.name);
});

// Filtrar
auto highTemp = sensors.filter([](const Sensor& s) {
    return s.name == "Temperature" && s.value > 30.0f;
});

// Transformar
auto names = sensors.transform<std::string>([](const Sensor& s) {
    return s.name;
});

// Ordenar por valor
sensors.sort([](const Sensor& a, const Sensor& b) {
    return a.value < b.value;
});
```

---

## NativeQueue

Fila usando FreeRTOS queue nativa - ideal para alta performance e ISR.

### Requisitos

- Tipo `T` deve ser trivialmente copiável (`std::is_trivially_copyable_v<T>`)

### API

```cpp
template<typename T>
class NativeQueue {
public:
    explicit NativeQueue(size_t length);
    ~NativeQueue();
    
    bool isValid() const;
    bool empty() const;
    size_t size() const;
    size_t available() const;
    bool full() const;
    void clear();
    
    // Operações bloqueantes
    bool send(const T& item, uint32_t timeoutMs = portMAX_DELAY);
    bool sendToFront(const T& item, uint32_t timeoutMs = portMAX_DELAY);
    bool receive(T& item, uint32_t timeoutMs = portMAX_DELAY);
    std::optional<T> receive(uint32_t timeoutMs = 0);
    bool peek(T& item) const;
    
    // Operações ISR-safe
    bool sendFromISR(const T& item, BaseType_t* higherPriorityTaskWoken = nullptr);
    bool receiveFromISR(T& item, BaseType_t* higherPriorityTaskWoken = nullptr);
};
```

### Exemplo

```cpp
using namespace SafeContainers;

struct SensorReading {
    uint32_t timestamp;
    float value;
};

NativeQueue<SensorReading> readings(50);

// Produtor (pode ser ISR)
void IRAM_ATTR sensorISR() {
    SensorReading reading = {esp_timer_get_time(), readADC()};
    BaseType_t woken = pdFALSE;
    readings.sendFromISR(reading, &woken);
    portYIELD_FROM_ISR(woken);
}

// Consumidor (task normal)
void processorTask(void* param) {
    SensorReading reading;
    while (true) {
        // Bloqueia até receber (máximo 1 segundo)
        if (readings.receive(reading, 1000)) {
            processReading(reading);
        }
    }
}
```

---

## Padrões de Uso

### Producer-Consumer com SafeQueue

```cpp
SafeQueue<Task> taskQueue(100);

// Produtor
void addTask(const Task& task) {
    if (!taskQueue.push(task)) {
        ESP_LOGW("PROD", "Fila cheia, descartando task antiga");
        taskQueue.pushOverwrite(task);
    }
}

// Consumidor
void workerTask(void* param) {
    while (true) {
        if (auto task = taskQueue.pop()) {
            executeTask(*task);
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}
```

### Cache com SafeMap

```cpp
SafeMap<std::string, CachedData> cache;

CachedData getData(const std::string& key) {
    // Tenta cache primeiro
    if (auto cached = cache.get(key)) {
        return *cached;
    }
    
    // Busca e armazena
    CachedData data = fetchFromSource(key);
    cache.insert(key, data);
    return data;
}

void invalidateCache(const std::string& key) {
    cache.erase(key);
}
```

### Iteração Segura com Guard

```cpp
SafeList<Connection*> connections;

void broadcastMessage(const std::string& msg) {
    auto guard = connections.iterationGuard();
    if (guard) {
        for (auto* conn : guard) {
            if (conn && conn->isActive()) {
                conn->send(msg);
            }
        }
    }
}
```

---

## Dependências

```cmake
idf_component_register(
    SRCS
        "ScopedLock.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        freertos
)
```

## Compatibilidade

- ESP-IDF >= 5.0.0
- C++17 ou superior
- FreeRTOS
- Plataformas: ESP32, ESP32-S2, ESP32-S3, ESP32-C3, ESP32-C6
