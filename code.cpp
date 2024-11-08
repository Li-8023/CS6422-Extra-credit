#include <iostream>
#include <map>
#include <vector>
#include <fstream>
#include <iostream>
#include <chrono>
#include <string>
#include <list>
#include <unordered_map>
#include <iostream>
#include <map>
#include <string>
#include <memory>
#include <sstream>
#include <limits>
#include <thread>
#include <queue>
#include <cassert>
#include <cstring>
#include <thread>

enum FieldType { INT, FLOAT, STRING };

// Define a basic Field variant class that can hold different types
class Field {
public:
    FieldType type;
    std::unique_ptr<char[]> data;
    size_t data_length;

public:
    Field(int i) : type(INT) { 
        data_length = sizeof(int);
        data = std::make_unique<char[]>(data_length);
        std::memcpy(data.get(), &i, data_length);
    }

    Field(float f) : type(FLOAT) { 
        data_length = sizeof(float);
        data = std::make_unique<char[]>(data_length);
        std::memcpy(data.get(), &f, data_length);
    }

    Field(const std::string& s) : type(STRING) {
        data_length = s.size() + 1;  // include null-terminator
        data = std::make_unique<char[]>(data_length);
        std::memcpy(data.get(), s.c_str(), data_length);
    }

    Field &operator=(const Field &other)
    {
        if (&other == this)
        {
            return *this;
        }
        type = other.type;
        data_length = other.data_length;
        data = std::make_unique<char[]>(data_length);
        std::memcpy(data.get(), other.data.get(), data_length);
        return *this;
    }

    Field(const Field &other)
    {
        type = other.type;
        data_length = other.data_length;
        data = std::make_unique<char[]>(data_length);
        std::memcpy(data.get(), other.data.get(), data_length);
    }
    Field(Field &&other) noexcept
    {
        type = other.type;
        data_length = other.data_length;
        data = std::move(other.data);
    }

    FieldType getType() const { return type; }
    int asInt() const { 
        return *reinterpret_cast<int*>(data.get());
    }
    float asFloat() const { 
        return *reinterpret_cast<float*>(data.get());
    }
    std::string asString() const { 
        return std::string(data.get());
    }

    std::string serialize() {
        std::stringstream buffer;
        buffer << type << ' ' << data_length << ' ';
        if (type == STRING) {
            buffer << data.get() << ' ';
        } else if (type == INT) {
            buffer << *reinterpret_cast<int*>(data.get()) << ' ';
        } else if (type == FLOAT) {
            buffer << *reinterpret_cast<float*>(data.get()) << ' ';
        }
        return buffer.str();
    }

    void serialize(std::ofstream& out) {
        std::string serializedData = this->serialize();
        out << serializedData;
    }

    static std::unique_ptr<Field> deserialize(std::istream& in) {
        int type; in >> type;
        size_t length; in >> length;
        if (type == STRING) {
            std::string val; in >> val;
            return std::make_unique<Field>(val);
        } else if (type == INT) {
            int val; in >> val;
            return std::make_unique<Field>(val);
        } else if (type == FLOAT) {
            float val; in >> val;
            return std::make_unique<Field>(val);
        }
        return nullptr;
    }

    void print() const{
        switch(getType()){
            case INT: std::cout << asInt(); break;
            case FLOAT: std::cout << asFloat(); break;
            case STRING: std::cout << asString(); break;
        }
    }
};

class Tuple {
public:
    std::vector<std::unique_ptr<Field>> fields;

    void addField(std::unique_ptr<Field> field) {
        fields.push_back(std::move(field));
    }

    size_t getSize() const {
        size_t size = 0;
        for (const auto& field : fields) {
            size += field->data_length;
        }
        return size;
    }

    std::string serialize() {
        std::stringstream buffer;
        buffer << fields.size() << ' ';
        for (const auto& field : fields) {
            buffer << field->serialize();
        }
        return buffer.str();
    }

    void serialize(std::ofstream& out) {
        std::string serializedData = this->serialize();
        out << serializedData;
    }

    static std::unique_ptr<Tuple> deserialize(std::istream& in) {
        auto tuple = std::make_unique<Tuple>();
        size_t fieldCount; in >> fieldCount;
        for (size_t i = 0; i < fieldCount; ++i) {
            tuple->addField(Field::deserialize(in));
        }
        return tuple;
    }

    void print() const {
        for (const auto& field : fields) {
            field->print();
            std::cout << " ";
        }
        std::cout << "\n";
    }
};

static constexpr size_t PAGE_SIZE = 8192;  // Fixed page size
static constexpr size_t MAX_SLOTS = 20;   // Fixed number of slots
uint16_t INVALID_VALUE = std::numeric_limits<uint16_t>::max(); // Sentinel value

struct Slot {
    bool empty = true;                 // Is the slot empty?    
    uint16_t offset = INVALID_VALUE;    // Offset of the slot within the page
    uint16_t length = INVALID_VALUE;    // Length of the slot
};

// Slotted Page class
class SlottedPage {
public:
    std::unique_ptr<char[]> page_data = std::make_unique<char[]>(PAGE_SIZE);
    size_t metadata_size = sizeof(Slot) * MAX_SLOTS;
    virtual ~SlottedPage() = default;
    std::vector<size_t> free_slots;

    SlottedPage(){
        // Empty page -> initialize slot array inside page
        Slot* slot_array = reinterpret_cast<Slot*>(page_data.get());
        for (size_t slot_itr = 0; slot_itr < MAX_SLOTS; slot_itr++) {
            slot_array[slot_itr].empty = true;
            slot_array[slot_itr].offset = INVALID_VALUE;
            slot_array[slot_itr].length = INVALID_VALUE;
            free_slots.push_back(slot_itr);
        }
    }

    size_t calculateOffset(size_t slot_itr)
    {
        // Calculate the offset based on slot_itr
        // This is a simple implementation that calculates offsets sequentially
        return slot_itr * (PAGE_SIZE / MAX_SLOTS) + sizeof(Slot) * MAX_SLOTS;
    }
    // Add a tuple, returns true if it fits, false otherwise.
    bool addTuple(std::unique_ptr<Tuple> tuple)
    {
        if (free_slots.empty())
        {
            return false; // No available slots
        }

        size_t slot_itr = free_slots.back();
        free_slots.pop_back();

        Slot *slot_array = reinterpret_cast<Slot *>(page_data.get());
        auto serializedTuple = tuple->serialize();
        size_t tuple_size = serializedTuple.size();

        // Calculate the offset using the new function
        if (slot_array[slot_itr].offset == INVALID_VALUE)
        {
            slot_array[slot_itr].offset = calculateOffset(slot_itr);
        }

        if (slot_array[slot_itr].offset + tuple_size >= PAGE_SIZE)
        {
            slot_array[slot_itr].empty = true;
            slot_array[slot_itr].offset = INVALID_VALUE;
            free_slots.push_back(slot_itr);
            return false;
        }

        // Copy serialized data into the page
        std::memcpy(page_data.get() + slot_array[slot_itr].offset, serializedTuple.c_str(), tuple_size);
        slot_array[slot_itr].empty = false;

        return true;
    }

    void deleteTuple(size_t index) {
        Slot* slot_array = reinterpret_cast<Slot*>(page_data.get());
        size_t slot_itr = 0;
        for (; slot_itr < MAX_SLOTS; slot_itr++) {
            if(slot_itr == index and
               slot_array[slot_itr].empty == false){
                slot_array[slot_itr].empty = true;
                break;
               }
        }

        //std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    void print() const{
        Slot* slot_array = reinterpret_cast<Slot*>(page_data.get());
        for (size_t slot_itr = 0; slot_itr < MAX_SLOTS; slot_itr++) {
            if (slot_array[slot_itr].empty == false){
                assert(slot_array[slot_itr].offset != INVALID_VALUE);
                const char* tuple_data = page_data.get() + slot_array[slot_itr].offset;
                std::istringstream iss(tuple_data);
                auto loadedTuple = Tuple::deserialize(iss);
                std::cout << "Slot " << slot_itr << " : [";
                std::cout << (uint16_t)(slot_array[slot_itr].offset) << "] :: ";
                loadedTuple->print();
            }
        }
        std::cout << "\n";
    }
};

const std::string database_filename = "buzzdb.dat";

class StorageManager {
public:    
    std::fstream fileStream;
    size_t num_pages = 0;

public:
    StorageManager()
    {
        fileStream.open(database_filename, std::ios::in | std::ios::out | std::ios::binary);
        if (!fileStream)
        {
            // If file does not exist, create it
            fileStream.clear(); // Reset the state
            fileStream.open(database_filename, std::ios::out | std::ios::binary);
            fileStream.close();
            fileStream.open(database_filename, std::ios::in | std::ios::out | std::ios::binary);
        }

        fileStream.seekg(0, std::ios::end);
        num_pages = fileStream.tellg() / PAGE_SIZE;

        std::cout << "Storage Manager :: Num pages: " << num_pages << "\n";
        if (num_pages == 0)
        {
            extend();
        }
    }

    ~StorageManager() {
        if (fileStream.is_open()) {
            fileStream.close();
        }
    }

    // Read a page from disk
    std::unique_ptr<SlottedPage> load(uint16_t page_id) {
        fileStream.seekg(page_id * PAGE_SIZE, std::ios::beg);
        auto page = std::make_unique<SlottedPage>();
        // Read the content of the file into the page
        if(fileStream.read(page->page_data.get(), PAGE_SIZE)){
            //std::cout << "Page read successfully from file." << std::endl;
        }
        else{
            std::cerr << "Error: Unable to read data from the file. \n";
            exit(-1);
        }
        return page;
    }

    // Write a page to disk
    void flush(uint16_t page_id, const std::unique_ptr<SlottedPage>& page) {
        size_t page_offset = page_id * PAGE_SIZE;        

        // Move the write pointer
        fileStream.seekp(page_offset, std::ios::beg);
        fileStream.write(page->page_data.get(), PAGE_SIZE);        
        fileStream.flush();
    }

    // Extend database file by one page
    void extend() {
        // std::cout << "Extending database file \n";

        // Create a slotted page
        auto empty_slotted_page = std::make_unique<SlottedPage>();

        // Move the write pointer
        fileStream.seekp(0, std::ios::end);

        // Write the page to the file, extending it
        fileStream.write(empty_slotted_page->page_data.get(), PAGE_SIZE);
        fileStream.flush();

        // Update number of pages
        num_pages += 1;
    }

};

using PageID = uint16_t;

class Policy {
public:
    virtual void touch(PageID page_id) = 0;
    virtual PageID evict() = 0;
    virtual ~Policy() = default;
};

class FifoPolicy : public Policy {
private:
    std::queue<PageID> queue;

public:
    void touch(PageID page_id) override {
        // For FIFO, we simply enqueue a newly touched page
        queue.push(page_id);
    }

    PageID evict() override {
        // Dequeue the oldest page and return its ID
        PageID evictedPageId = INVALID_VALUE;
        if(queue.size() != 0){
            evictedPageId = queue.front();
            queue.pop();
        }
        return evictedPageId;
    }

};

class LruPolicy : public Policy {
private:
    // List to keep track of the order of use
    std::list<PageID> lruList;

    // Map to find a page's iterator in the list efficiently
    std::unordered_map<PageID, std::list<PageID>::iterator> map;

public:
    void touch(PageID page_id) override {
        // If page already in the list, remove it
        if (map.find(page_id) != map.end()) {
            lruList.erase(map[page_id]);
            map.erase(page_id);
        }

        // Add the page to the front of the list
        lruList.emplace_front(page_id);
        map[page_id] = lruList.begin();
    }

    PageID evict() override {
        // Evict the least recently used page
        PageID evictedPageId = INVALID_VALUE;
        if(lruList.size() != 0){
            evictedPageId = lruList.back();
            map.erase(evictedPageId);
            lruList.pop_back();
        }
        return evictedPageId;
    }

};

constexpr size_t MAX_PAGES_IN_MEMORY = 5;

class BufferManager {
private:
    using PageMap = std::unordered_map<PageID, std::unique_ptr<SlottedPage>>;

    StorageManager storage_manager;
    PageMap pageMap;
    std::unique_ptr<Policy> policy;

public:
    BufferManager(): policy(std::make_unique<FifoPolicy>()) {}

    std::unique_ptr<SlottedPage>& getPage(int page_id) {
        auto it = pageMap.find(page_id);
        if (it != pageMap.end()) {
            policy->touch(page_id);
            return pageMap.find(page_id)->second;
        }

        if (pageMap.size() >= MAX_PAGES_IN_MEMORY) {
            auto evictedPageId = policy->evict();
            if(evictedPageId != INVALID_VALUE){
                // std::cout << "Evicting page " << evictedPageId << "\n";
                storage_manager.flush(evictedPageId, 
                                      pageMap[evictedPageId]);
            }
        }

        auto page = storage_manager.load(page_id);
        policy->touch(page_id);
        // std::cout << "Loading page: " << page_id << "\n";
        pageMap[page_id] = std::move(page);
        return pageMap[page_id];
    }

    void flushPage(int page_id) {
        //std::cout << "Flush page " << page_id << "\n";
        storage_manager.flush(page_id, pageMap[page_id]);
    }

    void extend(){
        storage_manager.extend();
    }
    
    size_t getNumPages(){
        return storage_manager.num_pages;
    }

};

class Compressor
{
public:
    virtual std::string compress(const std::string &data) = 0;
    virtual std::string decompress(const std::string &data) = 0;
    virtual ~Compressor() = default;
};

class DictionaryEncoding : public Compressor
{
private:
    std::unordered_map<std::string, int> dictionary;
    std::vector<std::string> reverseDictionary;

public:
    std::string compress(const std::string &data) override
    {
        std::stringstream compressed;
        std::istringstream stream(data);
        std::string word;
        while (stream >> word)
        {
            if (dictionary.find(word) == dictionary.end())
            {
                int index = dictionary.size();
                dictionary[word] = index;
                reverseDictionary.push_back(word);
            }
            compressed << dictionary[word] << " ";
        }
        return compressed.str();
    }

    std::string decompress(const std::string &data) override
    {
        std::stringstream decompressed;
        std::istringstream stream(data);
        int index;
        while (stream >> index)
        {
            decompressed << reverseDictionary[index] << " ";
        }
        return decompressed.str();
    }
};

class RunLengthEncoding : public Compressor
{
public:
    std::string compress(const std::string &data) override
    {
        std::string compressed;
        compressed.reserve(data.size()); 

        char prevChar = data[0];
        int count = 1;
        for (size_t i = 1; i < data.size(); ++i)
        {
            if (data[i] == prevChar)
            {
                count++;
            }
            else
            {
                compressed += prevChar;
                compressed += std::to_string(count);
                prevChar = data[i];
                count = 1;
            }
        }
        compressed += prevChar;
        compressed += std::to_string(count);
        return compressed;
    }

    std::string decompress(const std::string &data) override
    {
        std::string decompressed;
        decompressed.reserve(data.size()); 

        for (size_t i = 0; i < data.size(); i += 2)
        {
            char ch = data[i];
            int count = data[i + 1] - '0';
            decompressed.append(count, ch);
        }
        return decompressed;
    }
};

class BatchCompressor : public Compressor 
{
private:
    std::vector<std::unique_ptr<Compressor>> compressors;

public:
    BatchCompressor()
    {
        compressors.push_back(std::make_unique<DictionaryEncoding>());
        compressors.push_back(std::make_unique<RunLengthEncoding>());
    }

    std::string compress(const std::string &data)
    {
        std::string compressedData = data;
        for (const auto &compressor : compressors)
        {
            compressedData = compressor->compress(compressedData);
        }
        return compressedData;
    }

    std::string decompress(const std::string &data)
    {
        std::string decompressedData = data;
        for (auto it = compressors.rbegin(); it != compressors.rend(); ++it)
        {
            decompressedData = (*it)->decompress(decompressedData);
        }
        return decompressedData;
    }
};

class CompressedSlottedPage : public SlottedPage
{
private:
    BatchCompressor batchCompressor;

public:
    bool addTuple(std::unique_ptr<Tuple> tuple)
    {
        auto serializedTuple = tuple->serialize();
        auto compressedTuple = batchCompressor.compress(serializedTuple);
        size_t tuple_size = compressedTuple.size();

        size_t slot_itr = 0;
        Slot *slot_array = reinterpret_cast<Slot *>(page_data.get());
        for (; slot_itr < MAX_SLOTS; slot_itr++)
        {
            if (slot_array[slot_itr].empty == true && slot_array[slot_itr].length >= tuple_size)
            {
                break;
            }
        }
        if (slot_itr == MAX_SLOTS)
        {
            return false;
        }

        slot_array[slot_itr].empty = false;
        size_t offset = INVALID_VALUE;
        if (slot_array[slot_itr].offset == INVALID_VALUE)
        {
            if (slot_itr != 0)
            {
                auto prev_slot_offset = slot_array[slot_itr - 1].offset;
                auto prev_slot_length = slot_array[slot_itr - 1].length;
                offset = prev_slot_offset + prev_slot_length;
            }
            else
            {
                offset = metadata_size;
            }
            slot_array[slot_itr].offset = offset;
        }
        else
        {
            offset = slot_array[slot_itr].offset;
        }

        if (offset + tuple_size >= PAGE_SIZE)
        {
            slot_array[slot_itr].empty = true;
            slot_array[slot_itr].offset = INVALID_VALUE;
            return false;
        }

        assert(offset != INVALID_VALUE);
        assert(offset >= metadata_size);
        assert(offset + tuple_size < PAGE_SIZE);

        if (slot_array[slot_itr].length == INVALID_VALUE)
        {
            slot_array[slot_itr].length = tuple_size;
        }

        std::memcpy(page_data.get() + offset, compressedTuple.c_str(), tuple_size);

        return true;
    }

    std::unique_ptr<Tuple> getTuple(size_t index)
    {
        Slot *slot_array = reinterpret_cast<Slot *>(page_data.get());
        if (index >= MAX_SLOTS || slot_array[index].empty)
        {
            return nullptr;
        }
        size_t offset = slot_array[index].offset;
        size_t length = slot_array[index].length;
        std::string compressedData(page_data.get() + offset, length);
        auto decompressedData = batchCompressor.decompress(compressedData);
        std::istringstream iss(decompressedData);
        return Tuple::deserialize(iss);
    }
};

class BuzzDB
{
    
public:
    BufferManager buffer_manager;
    size_t max_number_of_tuples = 5000;
    size_t tuple_insertion_attempt_counter = 0;
    std::unique_ptr<Compressor> compressor;
    std::vector<std::unique_ptr<Tuple>> tupleBatch;
    static constexpr size_t BATCH_SIZE = 100; 
    BuzzDB()
    {
        // Storage Manager automatically created
    }

    void setCompressor(std::unique_ptr<Compressor> comp)
    {
        compressor = std::move(comp);
    }

    bool shouldCompress(const Tuple &tuple)
    {
        return tuple.getSize() > 100;
    }

    bool try_to_insert(std::unique_ptr<Tuple> newTuple)
    {
        bool status = false;
        auto serializedTuple = newTuple->serialize();

        if (compressor && shouldCompress(*newTuple))
        {
            serializedTuple = compressor->compress(serializedTuple);
        }

        auto num_pages = buffer_manager.getNumPages();
        for (size_t page_itr = 0; page_itr < num_pages; page_itr++)
        {
            auto &page = buffer_manager.getPage(page_itr);

            std::istringstream iss(serializedTuple);
            auto tupleToInsert = Tuple::deserialize(iss);
            status = page->addTuple(std::move(tupleToInsert));

            if (status == true)
            {
                buffer_manager.flushPage(page_itr);
                break;
            }
        }
        return status;
    }

    void insert(int key, int value)
    {
        tuple_insertion_attempt_counter += 1;

        if (tuple_insertion_attempt_counter >= max_number_of_tuples)
        {
            return;
        }

        auto newTuple = std::make_unique<Tuple>();
        newTuple->addField(std::make_unique<Field>(key));
        newTuple->addField(std::make_unique<Field>(value));
        newTuple->addField(std::make_unique<Field>(132.04f));
        newTuple->addField(std::make_unique<Field>("buzzdb"));

        tupleBatch.push_back(std::move(newTuple));

        if (tupleBatch.size() >= BATCH_SIZE)
        {
            compressAndInsertBatch();
            tupleBatch.clear();
        }
    }

    void compressAndInsertBatch()
    {
        std::stringstream batchStream;
        for (const auto &tuple : tupleBatch)
        {
            batchStream << tuple->serialize();
        }

        std::string batchData = batchStream.str();

        if (compressor)
        {
            batchData = compressor->compress(batchData);
        }

        bool status = false;
        auto num_pages = buffer_manager.getNumPages();
        for (size_t page_itr = 0; page_itr < num_pages; page_itr++)
        {
            auto &page = buffer_manager.getPage(page_itr);

            // Deserialize each tuple and insert into the page
            std::istringstream batchInput(batchData);
            while (batchInput)
            {
                auto tuple = Tuple::deserialize(batchInput);
                if (tuple)
                {
                    status = page->addTuple(std::move(tuple));
                    if (!status)
                    {
                        break;
                    }
                }
            }

            if (status)
            {
                buffer_manager.flushPage(page_itr);
                break;
            }
        }

        if (!status)
        {
            buffer_manager.extend();
            compressAndInsertBatch(); 
        }
    }
};

int main()
{
    auto start_no_compression = std::chrono::high_resolution_clock::now();

    BuzzDB db_no_compression;

    std::ifstream inputFile("output.txt");
    if (!inputFile)
    {
        std::cerr << "Unable to open file" << std::endl;
        return 1;
    }

    int field1, field2;
    while (inputFile >> field1 >> field2)
    {
        db_no_compression.insert(field1, field2);
    }

    auto end_no_compression = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed_no_compression = end_no_compression - start_no_compression;
    std::cout << "Elapsed time (no compression): " << elapsed_no_compression.count() << " seconds" << std::endl;

    auto start_rle_compression = std::chrono::high_resolution_clock::now();

    BuzzDB db_rle_compression;
    db_rle_compression.setCompressor(std::make_unique<RunLengthEncoding>());

    inputFile.clear();
    inputFile.seekg(0, std::ios::beg);

    while (inputFile >> field1 >> field2)
    {
        db_rle_compression.insert(field1, field2);
    }

    auto end_rle_compression = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed_rle_compression = end_rle_compression - start_rle_compression;
    std::cout << "Elapsed time (with RunLengthEncoding compression): " << elapsed_rle_compression.count() << " seconds" << std::endl;

    auto start_dict_compression = std::chrono::high_resolution_clock::now();

    BuzzDB db_dict_compression;
    db_dict_compression.setCompressor(std::make_unique<DictionaryEncoding>());

    inputFile.clear();
    inputFile.seekg(0, std::ios::beg);

    while (inputFile >> field1 >> field2)
    {
        db_dict_compression.insert(field1, field2);
    }

    auto end_dict_compression = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed_dict_compression = end_dict_compression - start_dict_compression;
    std::cout << "Elapsed time (with DictionaryEncoding compression): " << elapsed_dict_compression.count() << " seconds" << std::endl;

    auto start_batch_compression = std::chrono::high_resolution_clock::now();

    BuzzDB db_batch_compression;
    db_batch_compression.setCompressor(std::make_unique<BatchCompressor>());

    inputFile.clear();
    inputFile.seekg(0, std::ios::beg);

    while (inputFile >> field1 >> field2)
    {
        db_batch_compression.insert(field1, field2);
    }

    auto end_batch_compression = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed_batch_compression = end_batch_compression - start_batch_compression;
    std::cout << "Elapsed time (with BatchCompressor compression): " << elapsed_batch_compression.count() << " seconds" << std::endl;

    return 0;
}

// int main()
// {
//     // Get the start time for non-compressed version
//     auto start_no_compression = std::chrono::high_resolution_clock::now();

//     BuzzDB db_no_compression;

//     std::ifstream inputFile("output.txt");
//     if (!inputFile)
//     {
//         std::cerr << "Unable to open file" << std::endl;
//         return 1;
//     }

//     int field1, field2;
//     while (inputFile >> field1 >> field2)
//     {
//         db_no_compression.insert(field1, field2);
//     }

//     auto end_no_compression = std::chrono::high_resolution_clock::now();
//     std::chrono::duration<double> elapsed_no_compression = end_no_compression - start_no_compression;
//     std::cout << "Elapsed time (no compression): " << elapsed_no_compression.count() << " seconds" << std::endl;

//     // Get the start time for compressed version
//     auto start_compression = std::chrono::high_resolution_clock::now();

//     BuzzDB db_compression;

//     inputFile.clear();
//     inputFile.seekg(0, std::ios::beg);

//     while (inputFile >> field1 >> field2)
//     {
//         db_compression.insert(field1, field2);
//     }

//     auto end_compression = std::chrono::high_resolution_clock::now();
//     std::chrono::duration<double> elapsed_compression = end_compression - start_compression;
//     std::cout << "Elapsed time (with compression): " << elapsed_compression.count() << " seconds" << std::endl;

//     return 0;
// }