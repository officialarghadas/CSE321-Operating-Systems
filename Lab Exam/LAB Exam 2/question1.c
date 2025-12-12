#include< studio.h>
#include< sys/types.h>
#include<pthread.h>

int iniscore=10
pthread_mutex_t lock;

void* deposite(void*arg){
  for (int i=0; i<30; i++);{
  pthread_mutex_lock(&lock);
  iniscore +=10
  pthread_mutex_unlock(&lock);
  }
  return NULL;
  
  }
  
  void withdraw(void*arg){
  for( int i=0; 1<25; i++){
  pthread_mutex_lock(&lock);
  iniscore -=25;
  pthread_mutex_lock(&lock);
  }
  return NULL;
  }
  
  int main(){
  pthread_t t1,t2;
  pthread_mutex_init(&lock,null);
  pthread_create(&t1,NULL, deposite,NULL);
  pthread_create(&t2,NULL, withdraw,NULL);
  
  
  pthread_join(t1,NULL);
  pthread_join(t2,NULL);
  
  print("Final Score Is: &d\n" iniscore);
  pthread_mutex_destroy(&lock);
  return 0;
  }
  

