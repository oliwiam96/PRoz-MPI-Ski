#include "mpi.h"
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#define MSG_SIZE 2
#define MSG_HELLO 100
#define TAG_REQ 123
#define TAG_ACK 456
#define TAG_RELEASE 789
#define Capacity 1000

//	TODO WHO IS GONNA RECEIVE A RELEASE MSG?!



int clockLamport = 0;
int stop = 0;


pthread_mutex_t	mutexClock = PTHREAD_MUTEX_INITIALIZER;

pthread_mutex_t mutexCond = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

typedef struct queue_element
{
    int id;
    int time;
    int weight;
    struct queue_element *next;
    struct queue_element *previous;
} queue_el;

struct data
{
    int rank; // my own rank
    int size; // how many skiers
    int myWeight; // my own weight
    int * tab_ack; // did I receive ack from a skier with that id? 1/0
    queue_el *head; // pointer to a head of a queue
};

queue_el * insert(queue_el *head, queue_el *insert_element)
{
    queue_el *current = head;
    int insert_next = 0;
    int i = 0;
    int first = 0;

    while (1)
    {
        if(head == NULL)
        {
            head = insert_element;
            first = 1;
            break;
        }

        i = i + 1;
        if(current->time < insert_element->time)
        {
            if(current->next != NULL)
            {
                current = current->next;
            }
            else
            {
                insert_next = 1;
                break;
            }
        }
        else if (current->time == insert_element->time)
        {
            if(current->id < insert_element->id)
            {
                if(current->next != NULL)
                {
                    current = current->next;
                }
                else
                {
                    insert_next = 1;
                    break;
                }
            }
            else
            {
                break;
            }
        }
        else
        {
            break;
        }
    }

    if((i == 1) && (insert_next == 0))
    {
        head->previous = insert_element;
        insert_element->next = head;
        head = insert_element;
    }
    else if (first != 1)
    {
        if(insert_next == 1)
        {
            current->next = insert_element;
            insert_element->previous = current;
        }
        else
        {
            insert_element->previous = current->previous;
            current->previous->next = insert_element;
            current->previous = insert_element;
            insert_element->next = current;
        }
    }

    return head;

}


queue_el * delete(queue_el *head, int id)
{
    queue_el *current = head;

    while(1)
    {
        if(current == NULL)
        {
            break;
        }

        if(current->id == id)
        {
            if(current->previous == NULL)
            {
                head = current->next;
            }
            else if(current->next == NULL)
            {
                current->previous->next = NULL;
            }
            else
            {
                current->next->previous = current->previous;
                current->previous->next = current->next;
            }
            free(current);
            break;
        }
        current = current->next;
    }

    return head;
}


void print(queue_el *head)
{
    queue_el *current = head;
    printf("KOLEJKA\n");

    while (current != NULL)
    {
        printf("id: %d time: %d\n", current->id, current->time);
        current = current->next;
    }
}
int checkWeights(queue_el *head, int myId)
{
    queue_el *current = head;
    printf("KOLEJKA\n");

    int sum = 0;
    while (current != NULL && current->id != myId)
    {

        sum = sum + current->weight;
        current = current->next;
    }
    return sum <= Capacity;
}

queue_el * new_element(int id, int time, int weight)
{
    queue_el *new = malloc(sizeof(queue_el));
    new->id = id;
    new->time = time;
    new->weight = weight;
    new->next = NULL;
    new->previous = NULL;

    return new;
}


// ten watek to 2 w naszym sprawku
void* receiveAndSendAck(void* arg)
{
    while(!stop)
    {
        MPI_Status status;
        int msg[MSG_SIZE], receivedClock, receivedWeight;
        struct data* dane = (struct data*)arg;
        MPI_Recv(msg, MSG_SIZE, MPI_INT, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
        receivedClock = msg[0];
        receivedWeight = msg[1];
        // semafor  P
        pthread_mutex_lock(&mutexClock);

        clockLamport = (clockLamport > receivedClock) ? clockLamport : receivedClock;
        clockLamport += 1;
        // semafor V
        pthread_mutex_unlock(&mutexClock);

        // wstaw do kolejki
        if(status.MPI_TAG == TAG_REQ)
        {
            pthread_mutex_lock(&mutexClock);
            dane->head = insert(dane->head, new_element(status.MPI_SOURCE, receivedClock, receivedWeight));
            clockLamport += 1;
            msg[0] = clockLamport;
            msg[1] = -1;
            MPI_Send(msg, MSG_SIZE, MPI_INT, status.MPI_SOURCE, TAG_ACK, MPI_COMM_WORLD);
            pthread_mutex_unlock(&mutexClock);
        }
        else if(status.MPI_TAG == TAG_ACK)
        {
            pthread_mutex_lock(&mutexCond);
            dane->tab_ack[status.MPI_SOURCE] = 1;
            int success = 1;
            for (int i = 0; i< dane->size; i++)
            {
                if (dane->tab_ack[i] != 1)
                {
                    success = 0;
                    break;
                }
            }
            if(success)
            {
                pthread_cond_signal(&cond); // Should wake up *one* thread
            }
            pthread_mutex_unlock(&mutexCond);
        }
        else if(status.MPI_TAG == TAG_RELEASE)
        {
            pthread_mutex_lock(&mutexClock);
            dane->head = delete(dane->head, status.MPI_SOURCE);
            pthread_mutex_unlock(&mutexClock);
        }


    }
    return NULL;
}

void* mainSkiThread(void* arg)
{
    while(!stop)
    {
        int msg[MSG_SIZE];
        struct data* dane = (struct data*)arg;
        int i;
        int receivedClock, receivedStatus;
        // semafor P
        pthread_mutex_lock(&mutexClock);
        clockLamport += 1;
        msg[0] = clockLamport;
        msg[1] = dane->myWeight;
        for(i = 0; i < dane->size; i++)
        {
            if(i != dane->rank) // do not send to yourself
            {
                MPI_Send(msg, MSG_SIZE, MPI_INT, i, TAG_REQ, MPI_COMM_WORLD);
            }
        }
        // semafor V
        //wstaw do kolejki wlasne zadanie
        dane->head = insert(dane->head, new_element(dane->rank, clockLamport, dane->myWeight));
        pthread_mutex_unlock(&mutexClock);
        //sprawdz warunek bazujacy na kolejce (suma wag) i czy od wszystkich ack

        pthread_mutex_lock(&mutexCond);
        do
        {
            int success = 1;
            for (int i = 0; i< dane->size; i++)
            {
                if (dane->tab_ack[i] != 1)
                {
                    success = 0;
                    break;
                }
            }
            if(success)
            {
                break;
            }
            else
            {
                pthread_cond_wait(&cond, &mutexCond);
            }


        }
        while(1);
        pthread_mutex_unlock(&mutexCond);
        // wyzerowanie ACK
        for (int i = 0; i < dane->size; i++)
        {
            dane->tab_ack[i] = 0;
        }
        // GO!
        sleep(5);

        // send RELEASE
        pthread_mutex_lock(&mutexClock);
        clockLamport += 1;
        msg[0] = clockLamport;
        for(i = 0; i < dane->size; i++)
        {
            if(i != dane->rank) // do not send to yourself
            {
                MPI_Send(msg, MSG_SIZE, MPI_INT, i, TAG_RELEASE, MPI_COMM_WORLD);
            }
        }
        // TODO sleep random
        //  usun swoje zadanie z kolejki
        dane->head = delete(dane->head, dane->rank);
        pthread_mutex_unlock(&mutexClock);
        sleep(5);

    }
    return NULL;
}



int main(int argc, char **argv)
{
    int rank,size;


    int provided = 0;
    MPI_Init_thread( &argc, &argv, MPI_THREAD_MULTIPLE,  &provided);
    if (provided < MPI_THREAD_MULTIPLE)
    {
        printf("ERROR: The MPI library does not have full thread support\n");

    }
    else
    {
        printf("Full support for multiple threads!\n");
    }


    MPI_Comm_rank( MPI_COMM_WORLD, &rank);
    MPI_Comm_size( MPI_COMM_WORLD, &size);
    struct data dane;
    dane.rank=rank;
    dane.size=size;
    dane.myWeight = 70 + (30 - (rand() % 60));
    dane.tab_ack = malloc(dane.size*sizeof(int));
    for (int i = 0; i < dane.size; i++)
    {
        dane.tab_ack[i] = 0;
    }
    pthread_t watek1,watek2;
    pthread_create(&watek1,NULL,receiveAndSendAck,&dane);
    pthread_create(&watek2,NULL,mainSkiThread,&dane);
    pthread_join(watek1,NULL);
    pthread_join(watek2,NULL);
    pthread_mutex_destroy(&mutexClock);
    pthread_cond_destroy(&cond);
    pthread_mutex_destroy(&mutexCond);
    free(dane.tab_ack);
    MPI_Finalize();
}
