#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Headers for xv6

// NO NULL IN XV6?

struct Node{
    int num;
    struct Node *next;
};

struct LL{
    struct Node *head;
};

int add(int a, int b) { return a+b; }

// sequential_compute for normal linux usage 
int sequential_compute(char *filename, int (*fp)(int, int)){
    // Initialize number array 
    struct LL nums;
    nums.head = NULL;

    FILE* fptr = fopen(filename, "r");
    if (fptr == NULL) { // Attempt to read file 
        printf("The file is not opened.");
    }
    else{
        // Read file line by line (assuming each number is on a separate line)
        while(!feof(fptr)){ 
            struct Node* new_node = (struct Node*)malloc(sizeof(struct Node));
            if (fscanf(fptr, "%d", &new_node->num) == 1) {  // Successfully read a number 
                new_node->next = nums.head; // Add to array
                nums.head = new_node;
            }else{
                free(new_node);
                break;  // Stop when no more data
            }
        }
        printf("File read successfully.\n");
        fclose(fptr);
    } 

    // Sequential addition 
    int sum = 0; // Initialize sum 

    // Check for an empty list
    if (nums.head == NULL) {
        return 0;
    }
    
    struct Node* current_num = nums.head;
    sum = current_num->num;

    current_num = current_num->next; // Start from the second node
    
    while(current_num != NULL){ // While we have not reached the end of the array 
        sum = fp(sum, current_num->num); // Summation using given function pointer 
        current_num = current_num->next; // Move to the next node
    }

    // The cleanup must be done with a separate loop after the calculation.
    struct Node* temp = nums.head;
    while (temp != NULL) {
        struct Node* node_to_free = temp;
        temp = temp->next; // Move to the next node before freeing the current one
        free(node_to_free);
    }
    nums.head = NULL; // Reset head after freeing all nodes

    return sum;
}

// main for normal linux usage 
int main(void){
    char filename[100];
    // char fp_name[50]; // function pointer name

    printf("Please enter the full path of the file containing your numbers:\n");
    printf("\n");
    scanf("%s", filename);

    // printf("Please enter the name of the function pointer you would like to use:\n");
    // printf("\n");
    // scanf("%s", fp_name);

    int result = sequential_compute(filename, add);

    printf("Result = %d\n", result);
    printf("\n");

    return 0;
}

// // sequential_compute for xv6 usage --- still not edited 
// int sequential_compute(char *filename, int (*add)(int, int)){
//     // Initialize number array 
//     struct LL nums;
//     nums.head = NULL;

//     FILE* fptr = fopen(filename, "r");
//     if (fptr == NULL) { // Attempt to read file 
//         printf("The file is not opened.");
//     }
//     else{
//         // Read file line by line (assuming each number is on a separate line)
//         while(!feof(fptr)){ 
//             struct Node* new_node = (struct Node*)malloc(sizeof(struct Node));
//             if (fscanf(fptr, &new_node->num) == 1) {  // Successfully read a number 
//                 new_node->next = nums.head; // Add to array
//                 nums.head = new_node;
//             }else 
//                 break;  // Stop when no more data
//         }
//         fclose(fptr);
//     } 

//     // Sequential addition 
//     int sum = 0; // Initialize sum 
//     struct Node* current_num = (struct Node*)malloc(sizeof(struct Node)); // Get first number 
//     current_num = nums.head;
//     while(current_num != NULL){ // While we have not reached the end of the array 
//         struct Node* next_num = (struct Node*)malloc(sizeof(struct Node)); // Get second number 
//         next_num = current_num->next;
//         sum = sum + add(current_num->num, next_num->num); // Summation using given function pointer 
//         current_num = current_num->next;
//     }

//     return sum;
// }

// // main for xv6 usage --- still not edited 
// int main(int argc, char* argv[]) {
//     if (argc < 2) {
//         printf(1, "Usage: sequential_compute filename function_name ...\n");
//         exit();
//     }

//     sequential_compute(argc, argv);

//     exit();
// }