import random

def generate_dataset(filename, num_entries):
    with open(filename, 'w') as f:
        for _ in range(num_entries):
            key = random.randint(1, 1000)
            value = random.randint(1, 1000)
            f.write(f"{key} {value}\n")

if __name__ == "__main__":
    dataset_filename = "output.txt"
    num_entries = 10
    generate_dataset(dataset_filename, num_entries)
    print(f"Dataset with {num_entries} entries generated in '{dataset_filename}'.")

# import random

# # Generate High Repetition Data
# def generate_high_repetition_data(num_records, filename="high_repetition.txt"):
#     with open(filename, "w") as f:
#         for i in range(num_records):
#             value = random.randint(0, 1)
#             f.write(f"{i} {value}\n")
#     print(f"High repetition data generated in {filename}")

# # Generate Categorical Data
# def generate_categorical_data(num_records, filename="categorical_data.txt"):
#     categories = ["A", "B", "C", "D", "E"] 
#     with open(filename, "w") as f:
#         for i in range(num_records):
#             category = random.choice(categories)
#             f.write(f"{i} {ord(category)}\n")
#     print(f"Categorical data generated in {filename}")

# # Generate Mixed Data
# def generate_mixed_data(num_records, filename="mixed_data.txt"):
#     categories = ["A", "B", "C", "D", "E"]
#     with open(filename, "w") as f:
#         for i in range(num_records):
#             if i % 3 == 0:
#                 value = ord(random.choice(categories))  
#             else:
#                 value = random.randint(0, 100)
#             f.write(f"{i} {value}\n")
#     print(f"Mixed data generated in {filename}")

# # Main function to generate all datasets
# def generate_all_datasets(num_records=10):
#     generate_high_repetition_data(num_records)
#     generate_categorical_data(num_records)
#     generate_mixed_data(num_records)

# # Generate datasets with 1000 records each
# generate_all_datasets(10)
