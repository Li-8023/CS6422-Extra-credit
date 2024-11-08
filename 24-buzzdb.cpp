#include <iostream>
#include <map>
#include <vector>
#include <fstream>
#include <iostream>
#include <chrono>
#include <cstring>
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

    Field& operator=(const Field& other) {
        if (&other == this) {
            return *this;
        }
        type = other.type;
        data_length = other.data_length;
        std::memcpy(data.get(), other.data.get(), data_length);
        return *this;
    }

    Field(Field&& other){
        type = other.type;
        data_length = other.data_length;
        std::memcpy(data.get(), other.data.get(), data_length);
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

    void serialize(std::ofstream &out)
    {
        out.write(reinterpret_cast<const char *>(&type), sizeof(type));
        out.write(reinterpret_cast<const char *>(&data_length), sizeof(data_length));
        out.write(data.get(), data_length);
    }

    static std::unique_ptr<Field> deserialize(std::istream &in)
    {
        FieldType type;
        in.read(reinterpret_cast<char *>(&type), sizeof(type));

        size_t length;
        in.read(reinterpret_cast<char *>(&length), sizeof(length));

        std::unique_ptr<char[]> data = std::make_unique<char[]>(length);
        in.read(data.get(), length);

        if (type == STRING)
        {
            return std::make_unique<Field>(std::string(data.get()));
        }
        else if (type == INT)
        {
            int value = *reinterpret_cast<int *>(data.get());
            return std::make_unique<Field>(value);
        }
        else if (type == FLOAT)
        {
            float value = *reinterpret_cast<float *>(data.get());
            return std::make_unique<Field>(value);
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

static constexpr size_t PAGE_SIZE = 4096;  // Fixed page size
static constexpr size_t MAX_SLOTS = 128;   // Fixed number of slots
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

    SlottedPage(){
        // Empty page -> initialize slot array inside page
        Slot* slot_array = reinterpret_cast<Slot*>(page_data.get());
        for (size_t slot_itr = 0; slot_itr < MAX_SLOTS; slot_itr++) {
            slot_array[slot_itr].empty = true;
            slot_array[slot_itr].offset = INVALID_VALUE;
            slot_array[slot_itr].length = INVALID_VALUE;
        }
    }

    // Add a tuple, returns true if it fits, false otherwise.
    bool addTuple(std::unique_ptr<Tuple> tuple)
    {
        auto serializedTuple = tuple->serialize();
        size_t tuple_size = serializedTuple.size();
        std::cout << "Attempting to add tuple of size " << tuple_size << " bytes\n";

        Slot *slot_array = reinterpret_cast<Slot *>(page_data.get());

        // Calculate the currently used space based on metadata and data in occupied slots.
        size_t used_space = metadata_size;
        size_t max_offset = metadata_size; // Start at metadata size

        // Iterate over each slot to account for used space
        for (size_t i = 0; i < MAX_SLOTS; ++i)
        {
            if (!slot_array[i].empty)
            {
                used_space += slot_array[i].length;
                max_offset = std::max(max_offset, static_cast<size_t>(slot_array[i].offset + slot_array[i].length));
            }
        }

        // Available space on the page
        size_t available_space = PAGE_SIZE - max_offset;

        // Add debug statements here to print the used and available space
        std::cout << "Used space: " << used_space << " bytes, metadata size: " << metadata_size << "\n";
        std::cout << "Available space: " << available_space << " bytes\n";

        if (tuple_size > available_space)
        {
            std::cout << "Not enough available space: " << available_space
                      << " bytes, required: " << tuple_size << " bytes\n";
            return false;
        }

        // Find an empty slot and assign it to the new tuple.
        for (size_t slot_itr = 0; slot_itr < MAX_SLOTS; slot_itr++)
        {
            if (slot_array[slot_itr].empty)
            {
                size_t offset = max_offset;

                // Ensure the offset stays within the bounds of PAGE_SIZE.
                if (offset + tuple_size > PAGE_SIZE)
                {
                    std::cout << "Error: Offset exceeds available page size. Unable to insert.\n";
                    return false;
                }

                // Update slot metadata
                slot_array[slot_itr].empty = false;
                slot_array[slot_itr].offset = offset;
                slot_array[slot_itr].length = tuple_size;

                // Add debug output here to track slot assignment
                std::cout << "Slot " << slot_itr << " assigned at offset " << offset
                          << " with tuple size " << tuple_size << "\n";

                // Insert the serialized tuple data at the calculated offset
                std::memcpy(page_data.get() + offset, serializedTuple.c_str(), tuple_size);

                // Update max_offset to reflect the newly inserted tuple
                max_offset = offset + tuple_size;

                std::cout << "Inserted tuple at offset " << offset << " in slot " << slot_itr << "\n";
                return true;
            }
        }

        std::cout << "No suitable empty slot found for tuple insertion\n";
        return false;
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
    StorageManager(){
        fileStream.open(database_filename, std::ios::in | std::ios::out);
        if (!fileStream) {
            // If file does not exist, create it
            fileStream.clear(); // Reset the state
            fileStream.open(database_filename, std::ios::out);
        }
        fileStream.close(); 
        fileStream.open(database_filename, std::ios::in | std::ios::out); 

        fileStream.seekg(0, std::ios::end);
        num_pages = fileStream.tellg() / PAGE_SIZE;

        std::cout << "Storage Manager :: Num pages: " << num_pages << "\n";        
        if(num_pages == 0){
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
        std::cout << "Extending database file \n";

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
    virtual bool touch(PageID page_id) = 0;
    virtual PageID evict() = 0;
    virtual ~Policy() = default;
};

void printList(std::string list_name, const std::list<PageID>& myList) {
        std::cout << list_name << " :: ";
        for (const PageID& value : myList) {
            std::cout << value << ' ';
        }
        std::cout << '\n';
}

class LruPolicy : public Policy {
private:
    // List to keep track of the order of use
    std::list<PageID> lruList;

    // Map to find a page's iterator in the list efficiently
    std::unordered_map<PageID, std::list<PageID>::iterator> map;

    size_t cacheSize;

public:

    LruPolicy(size_t cacheSize) : cacheSize(cacheSize) {}

    bool touch(PageID page_id) override {
        //printList("LRU", lruList);

        bool found = false;
        // If page already in the list, remove it
        if (map.find(page_id) != map.end()) {
            found = true;
            lruList.erase(map[page_id]);
            map.erase(page_id);            
        }

        // If cache is full, evict
        if(lruList.size() == cacheSize){
            evict();
        }

        if(lruList.size() < cacheSize){
            // Add the page to the front of the list
            lruList.emplace_front(page_id);
            map[page_id] = lruList.begin();
        }

        return found;
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

constexpr size_t MAX_PAGES_IN_MEMORY = 10;

class BufferManager {
private:
    using PageMap = std::unordered_map<PageID, std::unique_ptr<SlottedPage>>;

    StorageManager storage_manager;
    PageMap pageMap;
    std::unique_ptr<Policy> policy;

public:
    BufferManager(): 
    policy(std::make_unique<LruPolicy>(MAX_PAGES_IN_MEMORY)) {}

    std::unique_ptr<SlottedPage>& getPage(int page_id) {
        auto it = pageMap.find(page_id);
        if (it != pageMap.end()) {
            policy->touch(page_id);
            return pageMap.find(page_id)->second;
        }

        if (pageMap.size() >= MAX_PAGES_IN_MEMORY) {
            auto evictedPageId = policy->evict();
            if(evictedPageId != INVALID_VALUE){
                std::cout << "Evicting page " << evictedPageId << "\n";
                storage_manager.flush(evictedPageId, 
                                      pageMap[evictedPageId]);
            }
        }

        auto page = storage_manager.load(page_id);
        policy->touch(page_id);
        std::cout << "Loading page: " << page_id << "\n";
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

class HashIndex {
private:
    std::unordered_map<int, int> hash_index;

public:
    void insertOrUpdate(int key, int value) {
        auto it = hash_index.find(key);
        if (it != hash_index.end()) {
            // Key already exists, increment its value
            it->second += value;
        } else {
            // Key doesn't exist, insert it with the given value
            hash_index[key] = value;
        }
    }

    int getValue(int key) const {
        auto it = hash_index.find(key);
        return it != hash_index.end() ? it->second : -1; // Return -1 if key not found
    }

    void print() const{
        for (auto const& pair : hash_index) { 
            std::cout << "key: " << pair.first << ", sum: " << pair.second << '\n';
        }
    }

};

//Dictionary Encoding
class DictionaryEncoder {
private:
    std::unordered_map<std::string, int> dict;
    std::vector<std::string> reverse_dict;

public:
    int encode(const std::string& value){
        if(dict.find(value) == dict.end()){
            int index = dict.size();
            dict[value] = index;
            reverse_dict.push_back(value);
        }
        return dict[value]; 
    }

    std::string decode(int index){
        return reverse_dict[index];
    }
};

//Run-Length Encoding
class RunLengthEncoder{
public:
    std::vector<std::pair<int, int>> encode(const std::vector<int>& data){
        std::vector<std::pair<int, int>> encoded_data;
        int n = data.size();
        for(int i = 0; i < n; ++i){
            int count = 1;
            while(i + 1 < n && data[i] == data[i + 1]){
                ++count;
                ++i;

            }
            encoded_data.push_back({data[i], count});
        }
        return encoded_data;
    }

    std::vector<int> decode(const std::vector<std::pair<int, int>> &encoded_data)
    {
        std::vector<int> decoded_data;
        for (const auto &pair : encoded_data)
        {
            decoded_data.insert(decoded_data.end(), pair.second, pair.first);
        }
        return decoded_data;
    }
};

class BuzzDB {
public:
    HashIndex index;

    BufferManager buffer_manager;


public:
    size_t max_number_of_tuples = 5000;
    size_t tuple_insertion_attempt_counter = 0;

    DictionaryEncoder dict_encoder;
    RunLengthEncoder rle_encoder;

    BuzzDB(){
        // Storage Manager automatically created
    }

    void batchInsertData(const std::vector<int> &keys, const std::vector<int> &values)
    {
        std::cout << "Starting batchInsertData\n";
        std::vector<int> encoded_keys, encoded_values;

        for (const auto &key : keys)
        {
            encoded_keys.push_back(dict_encoder.encode(std::to_string(key)));
        }

        auto compressed_values = rle_encoder.encode(values);

        for (size_t i = 0; i < compressed_values.size(); i++)
        {
            auto &compressed_pair = compressed_values[i];

            auto newTuple = std::make_unique<Tuple>();
            newTuple->addField(std::make_unique<Field>(encoded_keys[i]));
            newTuple->addField(std::make_unique<Field>(compressed_pair.first));
            newTuple->addField(std::make_unique<Field>(compressed_pair.second));

            std::cout << "Inserting tuple " << i << "\n";
            bool status = try_to_insert(newTuple);

            if (!status)
            {
                std::cout << "Extending buffer for tuple " << i << "\n";
                buffer_manager.extend();
                status = try_to_insert(newTuple); // Try once after extending

                if (!status)
                {
                    std::cerr << "Error: Unable to insert tuple even after extending\n";
                    break; // Prevent infinite loop by breaking
                }
            }
        }
        std::cout << "Completed batchInsertData\n";
    }

    bool try_to_insert(std::unique_ptr<Tuple> &newTuple)
    {
        bool status = false;
        auto num_pages = buffer_manager.getNumPages();
        for (size_t page_itr = 0; page_itr < num_pages; page_itr++)
        {
            std::cout << "Trying to insert tuple in page " << page_itr << "\n";
            auto &page = buffer_manager.getPage(page_itr);
            status = page->addTuple(std::move(newTuple));

            if (status)
            {
                std::cout << "Inserted tuple in page " << page_itr << "\n";
                buffer_manager.flushPage(page_itr);
                break;
            }
            else
            {
                std::cout << "Failed to insert in page " << page_itr << "\n";
            }
        }

        if (!status)
        {
            std::cout << "Failed to insert tuple after checking all pages\n";
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

        std::vector<int> keys{key};
        std::vector<int> values{value};
        batchInsertData(keys, values);
    }

    void scanTableToBuildIndex()
    {
        std::cout << "Scanning table to build index\n";
        auto num_pages = buffer_manager.getNumPages();

        for (size_t page_itr = 0; page_itr < num_pages; page_itr++)
        {
            std::cout << "Accessing page: " << page_itr << "\n";
            auto &page = buffer_manager.getPage(page_itr);
            char *page_buffer = page->page_data.get();
            Slot *slot_array = reinterpret_cast<Slot *>(page_buffer);

            for (size_t slot_itr = 0; slot_itr < MAX_SLOTS; slot_itr++)
            {
                if (slot_array[slot_itr].empty == false)
                {
                    std::cout << "Found non-empty slot at index: " << slot_itr << "\n";
                    assert(slot_array[slot_itr].offset != INVALID_VALUE);
                    const char *tuple_data = page_buffer + slot_array[slot_itr].offset;
                    std::istringstream iss(tuple_data);
                    auto loadedTuple = Tuple::deserialize(iss);
                    int key = loadedTuple->fields[0]->asInt();
                    int value = loadedTuple->fields[1]->asInt();

                    index.insertOrUpdate(key, value);
                }
            }
        }
        std::cout << "Completed scanning table to build index\n";
    }

    // perform a SELECT ... GROUP BY ... SUM query
    void selectGroupBySum() {
       index.print();
    }

    void runTest(BuzzDB& db, const std::string& filename){
        std::cout << "Starting batch insert for " << filename << "\n";

        std::ifstream inputFile(filename);

        if (!inputFile)
        {
            std::cerr << "Unable to open file: " << filename << std::endl;
            return;
        }

        std::vector<int> keys, values;
        int key, value;

        while (inputFile >> key >> value)
        {
            keys.push_back(key);
            values.push_back(value);
        }

        // Measure insertion time
        auto start = std::chrono::high_resolution_clock::now();
        db.batchInsertData(keys, values);
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> insertion_time = end - start;
        std::cout << "Insertion time for " << filename << ": " << insertion_time.count() << " seconds\n";

        // Measure query time
        start = std::chrono::high_resolution_clock::now();
        db.scanTableToBuildIndex();
        db.selectGroupBySum();
        end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> query_time = end - start;
        std::cout << "Query time for " << filename << ": " << query_time.count() << " seconds\n";

        // Measure storage (file size)
        std::ifstream db_file("buzzdb.dat", std::ios::binary | std::ios::ate);
        if (db_file.is_open())
        {
            auto file_size = db_file.tellg();
            std::cout << "Storage size for " << filename << ": " << file_size << " bytes\n";
            db_file.close();
        }
    }

};

int main() {
    // Get the start time
    // auto start = std::chrono::high_resolution_clock::now();

    BuzzDB db;

    // std::ifstream inputFile("output.txt");

    // std::vector<int> keys, values;

    // if (!inputFile) {
    //     std::cerr << "Unable to open file" << std::endl;
    //     return 1;
    // }

    // int field1, field2;
    // while (inputFile >> field1 >> field2) {
    //     // db.insert(field1, field2);
    //     keys.push_back(field1);
    //     values.push_back(field2);
    // }

    // //perform batch insert
    // db.batchInsertData(keys, values);

    // db.scanTableToBuildIndex();
    
    // db.selectGroupBySum();

    // std::cout << "Num Pages: " << db.buffer_manager.getNumPages() << "\n";

    // // Get the end time
    // auto end = std::chrono::high_resolution_clock::now();

    // // Calculate and print the elapsed time
    // std::chrono::duration<double> elapsed = end - start;
    // std::cout << "Elapsed time: " << elapsed.count() << " seconds" << std::endl;

    std::cout << "Testing High Repetition Data:\n";
    db.runTest(db, "high_repetition.txt");

    std::cout << "\nTesting Categorical Data:\n";
    db.runTest(db, "categorical_data.txt");

    std::cout << "\nTesting Mixed Data:\n";
    db.runTest(db, "mixed_data.txt");

    return 0;
}