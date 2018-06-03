#include "mpi.h"
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#define MSG_SIZE 2
#define MSG_HELLO 100
#define TAG_REQ 123
#define TAG_ACK 456
#define TAG_RELEASE 789
#define Capacity 150
#define GOUPTIME 5

#include <algorithm>
#include <vector>

int clockLamport = 0;
int stop = 0;


pthread_mutex_t	mutexClock = PTHREAD_MUTEX_INITIALIZER;

pthread_cond_t cond = PTHREAD_COND_INITIALIZER;


using namespace std;


struct queue_element
{
    int id;
    int time;
    int weight;

    queue_element(int e_id, int e_time, int e_weight) : id(e_id), time(e_time), weight(e_weight) {}

    bool operator <(const queue_element & other) const
    {
        if(time == other.time)
        {
            return id < other.id;
        }
        else
        {
            return time < other.time;
        }
    }

};

struct data
{
    int rank; // my own rank
    int size; // how many skiers
    int myWeight; // my own weight
    int * tab_ack; // did I receive ack from a skier with that id? 1/0
    vector<queue_element> vect;// queue
};

data dane;

// wyswietl zawartosc kolejki
void print(const data & myData)
{
    printf("[Watek %d; zegar %d] KOLEJKA: ", myData.rank, clockLamport);
    for (auto const& elem : myData.vect)
    {
        printf("id: %d time: %d weight: %d; \t", elem.id, elem.time, elem.weight);
    }
    printf("KONIEC KOLEJKI\n");
}

int checkWeights(const data & myData)
{

    int sum = 0;
    for (auto const& elem : myData.vect)
    {
        if(elem.id == myData.rank)
        {
            break;
        }
        sum += elem.weight;
    }
    return sum;
}

void* receiveAndSendAck(void* arg)
{

    while (!stop)
    {
        MPI_Status status;
        int msg[MSG_SIZE], receivedClock, receivedWeight;
        //struct data* dane = (struct data*)arg;


        MPI_Recv(msg, MSG_SIZE, MPI_INT, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
        printf("[Wątek %d - ack] otrzymał wiadomość od  %d o TAGU: %d.  [zegar z wiadomosci = %d]\n", dane.rank, status.MPI_SOURCE, status.MPI_TAG, receivedClock);

        receivedClock = msg[0];
        receivedWeight = msg[1];

        // wstaw do kolejki
        if (status.MPI_TAG == TAG_REQ)
        {
            pthread_mutex_lock(&mutexClock);

            clockLamport = (clockLamport > receivedClock) ? clockLamport : receivedClock;
            clockLamport += 1;

            printf("[Wątek %d - ack] wstawia do kolejki zgłoszenie %d. [zegar = %d]\n", dane.rank, status.MPI_SOURCE, clockLamport);
            //dane.head = insert(dane.head, new_element(status.MPI_SOURCE, receivedClock, receivedWeight));
            dane.vect.push_back(queue_element(status.MPI_SOURCE, receivedClock, receivedWeight));
            sort(dane.vect.begin(), dane.vect.end());
            print(dane);
            //printf("[wątek %d] moja kolejka to: [zegar = %d]\n", dane.rank, clockLamport);
            //print(dane.head);

            msg[0] = clockLamport;
            msg[1] = -1;
            MPI_Send(msg, MSG_SIZE, MPI_INT, status.MPI_SOURCE, TAG_ACK, MPI_COMM_WORLD);
            pthread_mutex_unlock(&mutexClock);
        }
        else if (status.MPI_TAG == TAG_ACK)
        {
            printf("[Wątek %d - ack] ustawia w tablicy ack od  %d. [zegar = %d]\n", dane.rank, status.MPI_SOURCE, clockLamport);

            pthread_mutex_lock(&mutexClock);
            dane.tab_ack[status.MPI_SOURCE] = 1;

            //print tab_ack
            int j;
            printf("Tab_ack: [ %d", dane.tab_ack[0]);
            for (j = 1; j < dane.size; j++)
            {
                printf(", %d", dane.tab_ack[j]);
            }
            printf("]\n");


            int success = 1;
            int i;
            for (i = 0; i < dane.size; i++)
            {
                if (dane.tab_ack[i] != 1)
                {
                    success = 0;
                    break;
                }
            }
            //if ((checkWeights(dane.head, dane.rank) + dane.myWeight) > Capacity) {
            if ((checkWeights(dane) + dane.myWeight) > Capacity)
            {

                success = 0;
            }

            if (success)
            {
                printf("[Wątek %d - ack] ACK probuje wybudzić wątek :D [zegar = %d]\n", dane.rank, clockLamport);

                pthread_cond_signal(&cond); // Should wake up *one* thread
            }
            pthread_mutex_unlock(&mutexClock);
        }
        else if (status.MPI_TAG == TAG_RELEASE)
        {
            printf("[Wątek %d - ack] usuwa z kolejki zgłoszenie %d.[zegar = %d]\n", dane.rank, status.MPI_SOURCE, clockLamport);

            pthread_mutex_lock(&mutexClock);
            //dane.head = delete(dane.head, status.MPI_SOURCE);
            int idToRemove = status.MPI_SOURCE;
            dane.vect.erase(
                remove_if(dane.vect.begin(), dane.vect.end(), [&idToRemove](queue_element const & queue_element)
            {
                return queue_element.id == idToRemove;
            }),
            dane.vect.end() );
            sort(dane.vect.begin(), dane.vect.end());
            //print(dane);
            int success = 1;
            int i;
            for (i = 0; i < dane.size; i++)
            {
                if (dane.tab_ack[i] != 1)
                {
                    success = 0;
                    break;
                }
            }

            printf("[Wątek %d - ack] Suma wag: %d .[zegar = %d]\n", dane.rank, checkWeights(dane), clockLamport);

            //if ((checkWeights(dane.head, dane.rank) + dane.myWeight) > Capacity) {
            if ((checkWeights(dane) + dane.myWeight) > Capacity)
            {
                success = 0;
            }

            if (success)
            {
                printf("[Wątek %d - ack] RELEASE probuje wybudzić wątek :D [zegar = %d]\n", dane.rank, clockLamport);

                pthread_cond_signal(&cond); // Should wake up *one* thread
            }
            pthread_mutex_unlock(&mutexClock); // Zmiana- unlock po signal
        }


    }
    return NULL;
}

void* mainSkiThread(void* arg)
{

    while (!stop)
    {
        int msg[MSG_SIZE];
        //struct data* dane = (struct data*)arg;

        int i;
        int receivedClock, receivedStatus;
        // semafor P
        pthread_mutex_lock(&mutexClock);
        clockLamport += 1;
        msg[0] = clockLamport;
        msg[1] = dane.myWeight;
        printf("[Wątek %d - main] wysłała do wszystkich request. [zegar = %d]\n", dane.rank, clockLamport);

        for (i = 0; i < dane.size; i++)
        {
            if (i != dane.rank) // do not send to yourself
            {
                MPI_Send(msg, MSG_SIZE, MPI_INT, i, TAG_REQ, MPI_COMM_WORLD);

            }
        }
        // semafor V
        //wstaw do kolejki wlasne zadanie
        //printf("[Wątek %d - main] chce wstawić do swojej kolejki swój request. [zegar = %d]\n", dane->rank, clockLamport);

        //dane->head = insert(dane->head, new_element(dane->rank, clockLamport, dane->myWeight));
        dane.vect.push_back(queue_element(dane.rank, clockLamport, dane.myWeight));
        sort(dane.vect.begin(), dane.vect.end());

        printf("[Wątek %d - main] wstawił do swojej kolejki swój request. [zegar = %d]\n", dane.rank, clockLamport);

        //sprawdz warunek bazujacy na kolejce (suma wag) i czy od wszystkich ack

        printf("[Wątek %d - main] sprawdza czy wszytskie wątki odebrały wiadomości. [zegar = %d]\n", dane.rank, clockLamport);
        do
        {
            int success = 1;
            int i;
            for (i = 0; i < dane.size; i++)
            {
                if (dane.tab_ack[i] != 1)
                {
                    success = 0;
                    break;
                }
            }
            //if ((checkWeights(dane->head, dane->rank) + dane->myWeight) > Capacity) {
            if ((checkWeights(dane) + dane.myWeight) > Capacity)
            {
                success = 0;
            }

            if (success)
            {
                break;
            }
            else
            {
                printf("[Wątek %d - main] zasypiam sobie... zzz... [zegar = %d]\n", dane.rank, clockLamport);

                pthread_cond_wait(&cond, &mutexClock);
            }


        }
        while (1);

        //dodałem tutaj ten mutex unlock
        pthread_mutex_unlock(&mutexClock);

        printf("[Wątek %d - main] wszytskie wątki odebrały moją wiadomość wiadomości. [zegar = %d] Pozdrawiam, watek %d\n", dane.rank, clockLamport, dane.rank);

        printf("[Wątek %d - main] wyzerowuje tablice ack i wejeżdza do góry. SEKCJA KRYTYCZNA [zegar = %d]\n", dane.rank, clockLamport);
        // wyzerowanie ACK- mutex niepotrzebny, bo póki nie wyślę nowego REQUESTA, to nikt nie odpowie ACK
        for (i = 0; i < dane.size; i++)
        {
            dane.tab_ack[i] = 0;
        }
        dane.tab_ack[dane.rank] = 1; //set ack to 1 from yourself
        // GO!
        printf("\n[Wątek %d - main] wjeżdzam do góry przez %d sekund [zegar = %d]\n\n", dane.rank, GOUPTIME, clockLamport);
        sleep(GOUPTIME);

        // send RELEASE
        pthread_mutex_lock(&mutexClock);
        msg[0] = clockLamport;
        printf("\n[Wątek %d - main] KONIEC SEKCJI KRYTYCZNEJ [zegar = %d]\n\n", dane.rank, clockLamport);

        for (i = 0; i < dane.size; i++)
        {
            if (i != dane.rank) // do not send to yourself
            {
                MPI_Send(msg, MSG_SIZE, MPI_INT, i, TAG_RELEASE, MPI_COMM_WORLD);
            }
        }
        printf("[Wątek %d - main] wysyłał release synał do wszystkich i zjeżdża na dół. [zegar = %d]\n", dane.rank, clockLamport);

        // sleep random przy zjeździe
        printf("[Wątek %d - main] usuwa swoje zgłoszenie ze swojej kolejki. [zegar = %d]\n", dane.rank, clockLamport);

        //  usun swoje zadanie z kolejki
        //dane->head = delete(dane->head, dane->rank);
        int idToRemove = dane.rank;
        dane.vect.erase(
            remove_if(dane.vect.begin(), dane.vect.end(), [&idToRemove](queue_element const & queue_element)
        {
            return queue_element.id == idToRemove;
        }),
        dane.vect.end() );
        sort(dane.vect.begin(), dane.vect.end());
        pthread_mutex_unlock(&mutexClock);

        int randomTime = 8 - (rand() % 7);
        printf("[Wątek %d - main] zjedża z góry przez %d sekund................ [zegar = %d]\n", dane.rank, randomTime, clockLamport);
        sleep(randomTime); // czy to jest potzrebne?
        printf("[Wątek %d - main] zjechał i znowy  ustawia się do kolejki narciarzy. [zegar = %d]\n", dane.rank, clockLamport);

    }
    return NULL;
}





int main(int argc, char **argv)
{
    srand(time(NULL));
    int rank, size;


    int provided = 0;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
    if (provided < MPI_THREAD_MULTIPLE)
    {
        printf("ERROR: The MPI library does not have full thread support\n");

    }
    else
    {
        printf("Full support for multiple threads!\n");
    }


    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    dane.rank = rank;
    dane.size = size;
    vector<queue_element> vect;
    dane.vect = vect;
    srand(rank);
    dane.myWeight = 70 + (30 - (rand() % 60));
    dane.tab_ack = (int*) malloc(dane.size * sizeof(int));
    int i;
    for (i = 0; i < dane.size; i++)
    {
        dane.tab_ack[i] = 0;
    }
    dane.tab_ack[dane.rank] = 1; // set ack from yourself to 1

    printf("Wątek %d zainicjował zmienne (waga = %d) i rozpocząl działnie.\n", dane.rank, dane.myWeight);
    pthread_t watek1, watek2;
    pthread_create(&watek1, NULL, receiveAndSendAck, &dane);
    pthread_create(&watek2, NULL, mainSkiThread, &dane);
    pthread_join(watek1, NULL);
    pthread_join(watek2, NULL);
    pthread_mutex_destroy(&mutexClock);
    pthread_cond_destroy(&cond);
    free(dane.tab_ack);
    MPI_Finalize();
    printf("Koniec programu");
    return 0;
}
