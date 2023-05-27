/* server process */

/* include the necessary header files */
#include<ctype.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<stdlib.h>
#include<arpa/inet.h>
#include<stdio.h>
#include<unistd.h>
#include<string.h>
#include<sys/select.h>

#include "protocol.h"
#include "libParseMessage.h"
#include "libMessageQueue.h"

int fdcount = 0; // Number of alive fds stored in fdlist.
int received = 0; // The number used to transfer the startPoint of the target user's fromClient message into the method recvMessage. Also, it can be used to update the target user's startPoint.
int checkss = 0;
/**
 * send a single message to client 
 * sockfd: the socket to read from
 * toClient: a buffer containing a null terminated string with length at most 
 * 	     MAX_MESSAGE_LEN-1 characters. We send the message with \n replacing \0
 * 	     for a mximmum message sent of length MAX_MESSAGE_LEN (including \n).
 * return 1, if we have successfully sent the message
 * return 2, if we could not write the message
 */
int sendMessage(int sfd, char *toClient){
    char curr;
    int count = 0;
    int len = strlen(toClient) + 1; // The number of character that is going to send to client, include '\n'.
    char c[len+1]; // The buffer used to store the message that is going to send to client.
    while (count < MAX_MESSAGE_LEN || count < len){
        curr = toClient[count];
        if(curr=='\0')curr='\n'; // Check '\0' and '\n'.
        c[count] = curr;
        if(curr=='\n')break;
        count++;
    }
    int numSend = send(sfd, c, len, 0); // Send message to user.
    if(numSend < count+1)return(2);
    return(1);
}


/**
 * read a single message from the client. 
 * sockfd: the socket to read from
 * fromClient: a buffer of MAX_MESSAGE_LEN characters to place the resulting message
 *             the message is converted from newline to null terminated, 
 *             that is the trailing \n is replaced with \0
 * return 1, if we have received a newline terminated string
 * return 2, if the socket closed (read returned 0 characters)
 * return 3, if we have read more bytes than allowed for a message by the protocol
 */
int recvMessage(int sfd, char *fromClient){
    char c[MAX_MESSAGE_LEN]; // The buffer used to store the received message from client.
    int numRecv = recv(sfd, c, MAX_MESSAGE_LEN, 0); // Receive message.
    if(numRecv==0) {
        return(2); // return 2, if the socket closed (read returned 0 characters).
    }
    int entered = 0; // The number used to count the number of next line character in message.
    int count = 0; // Loop through the buffer to check the newline character in the message.
    while (count < numRecv){
        
        if (c[count] == '\n' || c[count] == '\0'){
            fromClient[count + received] = '\0';
            entered++;
            if (count == numRecv-1 && entered == 1) return(1);
            count++;
        }
        else{
            fromClient[count + received] = c[count];
            count++;
        }
        
    }
    if (entered == 0 && (numRecv + received) >= MAX_MESSAGE_LEN) return(3); // If the message is too long, the message is illegal.
    received += numRecv;
    return(4); // Special cases with not complete message.
}

typedef struct { // User struct.
    char name[MAX_USER_LEN+1]; // The name of the registered client.
    MessageQueue queue; // Message queue.
    int userfd; // The fd of current client.
    char fromClient[MAX_MESSAGE_LEN]; // The buffer stores the message that received from the current client.
    char toClient[MAX_MESSAGE_LEN]; // The buffer stores the message that is going to send to current client.
    int startPoint; // The number of character of the not complete message that stored in the fromClient.
    int sendNum;	
    int sendIndex;
} USER;

/*
The method doParse checks the message from client, if the message fits with the instruction set,
send the relative responses to client.
Parameter:
    fdlist: The list stores the connected fd.
    userlist: The list stores all the user objects.
    target: The message that want to be parsed.
    i: the index of the target user in the userlist.
*/
int doParse(int *fdlist, USER *userlist, char *target, int i, fd_set writefds){
    char sendBag[MAX_MESSAGE_LEN]; // The buffer used to store the message temporarily.
    sendBag[0] = '\0';
    char *part[4];
    int numParts=parseMessage(target, part);
    int quitState = 0; // The number used to check if the client want to quit, 0 means False, 1 means True.

    if(numParts==0){
        strcpy(sendBag,"ERROR");
    } else if(strcmp(part[0], "list")==0){
        int index = 0;
        int listed = 0;
        char format[1024];
        format[0] = '\0';
        strcat(format, "users:");
        while (index < fdcount){ // Loop through the client list to get all the alive connections.
            if (strcmp(userlist[index].name, "\0")!=0) listed++;
            strcat(format, userlist[index].name);
            if (listed == 10) break; // Limit to print 10 clients.
            // Not include the clients that are not registered.
            if (index != fdcount-1 && strcmp(userlist[index].name, "\0") != 0) strcat(format, " ");
            index++;
        }
        strcpy(sendBag, format);
        format[0] = '\0';

    } else if(strcmp(part[0], "message")==0){
        char *fromUser=part[1];
        char *toUser=part[2];
        char *message=part[3];
        
        if(strcmp(fromUser, userlist[i].name)!=0){
            sprintf(sendBag, "invalidFromUser:%s",fromUser);
        } 
        else if (strcmp(fromUser, "\0") == 0){ // If the client not yet registered, not allow to send message.
            strcpy(sendBag, "Error: User not registered.");
        }
        else if (strcmp(toUser, "\0") == 0){ // Can not send message to the client with empty name.
            sprintf(sendBag, "invalidToUser:%s", toUser);
        }
        else if(strcmp(toUser, userlist[i].name)!=0){
            //To someone else
            sprintf(sendBag, "%s:%s:%s:%s","message", fromUser, toUser, message);
            int count = 0;
            int found = 1;
            while (count < fdcount){ // Loop through all the clients and find the toUser.
                if (strcmp(userlist[count].name, toUser)==0){ // Found the toUser.
                    if(enqueue(&userlist[count].queue, sendBag)){
                        strcpy(sendBag, "messageQueued");
                    }else{
                        strcpy(sendBag, "messageNotQueued");
                    }
                    found = 0;
                }
                count++;
            }

            if (found == 1){ // Not found.
                sprintf(sendBag, "invalidToUser:%s", toUser);
            }

        } else {
            sprintf(sendBag, "%s:%s:%s:%s","message", fromUser, toUser, message);
            if(enqueue(&userlist[i].queue, sendBag)){
                strcpy(sendBag, "messageQueued");
            }else{
                strcpy(sendBag, "messageNotQueued");
            }
        }
    } else if(strcmp(part[0], "quit")==0){
        strcpy(sendBag, "closing");
        quitState = 1;    
        
    } else if(strcmp(part[0], "getMessage")==0){

        if(dequeue(&userlist[i].queue, sendBag)){
        } else {
            strcpy(sendBag, "noMessage");
        }
    } else if(strcmp(part[0], "register")==0) {
        if (strcmp(userlist[i].name, "\0") != 0) { // The client allready registed, not allow to rename.
            strcpy(sendBag,"ERROR");
        }
        else if (strcmp(part[1], "\0") == 0) {
            strcpy(sendBag, "ERROR: User name can not be empty");
        }
        else if (strlen(part[1]) >= (MAX_USER_LEN+1)){
            strcpy(sendBag, "ERROR: The entered name is too long");
        }
        else{
            int compare = 1;
            int count = 0;
            while (count < fdcount){ // Compare and check all the registered users' names.
                if (strncmp(userlist[count].name, part[1], MAX_USER_LEN) == 0){
                    compare = 0;
                }
                count++;
            }
            if (compare!=0) { // If the name is not registered by other user, successfully registered.
                strcpy(userlist[i].name, part[1]);
                strcpy(sendBag, "registered");
            } else { // Fail to registered.
                strcpy(sendBag, "userAlreadyRegistered");
            }
        }
    }
    
    int nextLen = strlen(sendBag) + 1;
    if (userlist[i].sendIndex + nextLen >= MAX_MESSAGE_LEN) {
        // Client has illegal behavior, too many messages can not be sent to client, diconnect the client.
        close (fdlist[i]);
        int index = i;
        while (index < fdcount-1){
            fdlist[index] = fdlist[index+1];
            userlist[index] = userlist[index+1];
            index++;
        }
        fdcount--;
        return(0);
    }
    
    strcpy(&userlist[i].toClient[userlist[i].sendIndex], sendBag);
    userlist[i].sendNum++;
    userlist[i].sendIndex += nextLen; 
    
    if (FD_ISSET(fdlist[i], &writefds)) { // If the client's fd in writefds, we send all the messages to client.
        int count = 0; // number of messages sent.
        int curr = 0; // current character.
        int start = 0; // the start point of the message that is goint to send to user.
        while (count < userlist[i].sendNum) {
            if (userlist[i].toClient[curr] == '\0') {
                sendMessage(fdlist[i], &userlist[i].toClient[start]);
                start = curr + 1;
                count++;
            }
            curr++;
        }
        // Reset the toCient buffer.
        userlist[i].sendNum = 0;
        userlist[i].sendIndex = 0;
    }
    
    if (quitState == 1) {
        close (fdlist[i]);
        
        // Need to remove the user from userlist and fdlist when the user quit.
        int index = i;
        while (index < fdcount-1){
            fdlist[index] = fdlist[index+1];
            userlist[index] = userlist[index+1];
            index++;
        }
        fdcount--;
    }
    
    return(0);
}

int main (int argc, char ** argv) {
    int sockfd;

    if(argc!=2){
        fprintf(stderr, "Usage: %s portNumber\n", argv[0]);
        exit(1);
    }
    int port = atoi(argv[1]);

    if ((sockfd = socket (AF_INET, SOCK_STREAM, 0)) == -1) {
        perror ("socket call failed");
        exit (1);
    }

    struct sockaddr_in server;
    server.sin_family=AF_INET;          // IPv4 address
    server.sin_addr.s_addr=INADDR_ANY;  // Allow use of any interface 
    server.sin_port = htons(port);      // specify port

    if (bind (sockfd, (struct sockaddr *) &server, sizeof(server)) == -1) {
        perror ("bind call failed");
        exit (1);
    }

    if (listen (sockfd, 5) == -1) {
        perror ("listen call failed");
        exit (1);
    }
    
    int fdlist[33];
    USER userlist[33];
    fdlist[fdcount++]=sockfd;

    for (;;) {
        fd_set readfds, writefds, exceptfds;
        FD_ZERO(&readfds);
        FD_ZERO(&writefds);
        FD_ZERO(&exceptfds);
		
        int fdmax = 0;
        for (int i=0; i<fdcount; i++){
            if (fdlist[i]>0){
                FD_SET(fdlist[i], &readfds);
                FD_SET(fdlist[i], &writefds);
                if (fdlist[i] > fdmax){
                    fdmax = fdlist[i];
                }
            }

        }
        
        struct timeval tv;
        tv.tv_sec=5;
        tv.tv_usec=0;

        int numfds;
        if ((numfds=select(fdmax+1, &readfds, &writefds, &exceptfds, &tv))>0){
            for(int i = 0; i<fdcount; i++){
                if (FD_ISSET(fdlist[i], &readfds)){
                    if (fdlist[i] == sockfd){
                        int newsockfd;
                        if ((newsockfd = accept (sockfd, NULL, NULL)) == -1) {
                            perror ("accept call failed");
                            continue;
                        }
                        else{
                            if (fdcount >= 33) {
                                close(newsockfd); // Not allow to register.
                            }
                            else {
                                // Initialize the message of the client.
                                userlist[fdcount].name[0]='\0'; // The client not yet register, so leave the name as empty.
                                initQueue(&userlist[fdcount].queue);
                                fdlist[fdcount]=newsockfd;
                                userlist[fdcount].startPoint = 0;
                                userlist[fdcount].userfd = fdlist[fdcount];
                                userlist[fdcount].sendNum = 0;
                                userlist[fdcount].sendIndex =0;
                                fdcount++;
                            }
                        }
                    }
                    else {
                        received = userlist[i].startPoint;
                        int retVal=recvMessage(fdlist[i], userlist[i].fromClient);

                        if(retVal==1){ // Successfully received legal message structure, need to check if the message can fit with the instruction set.
                            userlist[i].startPoint = 0; // Reset the startPoint, since the message is in normal case.
                            doParse(fdlist, userlist, userlist[i].fromClient, i, writefds);
                        }
                        else if (retVal == 3){
                            // Too many message from user.
                            sendMessage(fdlist[i], "connection closed");
                            sleep(1);
                            close(fdlist[i]);
                            int index = i;
                            while (index < fdcount){
                                fdlist[index] = fdlist[index+1];
                                userlist[index] = userlist[index+1];
                                index++;
                            }
                            fdcount--;
                        }
                        else if (retVal == 4) { // Special cases with incomplete message.
                            int check = 0; // Index of current character.
                            int last = 0; // The number shows the last newline character in the message.
                            while (check < received){
                                // Checkout all the complete messages and parse them, left with the incomplete message.
                                if (userlist[i].fromClient[check] == '\0'){ // The characters from index last+1 to index check is a complete message.
                                    char fromClient[check-last+1];
                                    fromClient[0] = '\0';
                                    if (last == 0) strncpy(fromClient, &userlist[i].fromClient[last], check-last+1);
                                    else strncpy(fromClient, &userlist[i].fromClient[last+1], check-last+1);
                                    doParse(fdlist, userlist, fromClient, i, writefds);
                                    last = check;
                                }
                                check++;
                            }
                            if (last > 0) { // Have not complete message at the end, we need to keep it in the fromClient.
                                userlist[i].startPoint = received - last - 1; // Update startPoint to the next index of the last character + 1 of the incomplete message.
                                
                                // Rearrange the fromClient buffer, so the buffer only contains incomplete message.
                                char *cut = &userlist[i].fromClient[last+1];
                                userlist[i].fromClient[0] = '\0';
                                strncpy(userlist[i].fromClient, cut, userlist[i].startPoint);
                            }
                            else userlist[i].startPoint = received;
                        }
                        else{
                            // Need to remove the user from userlist and fdlist when the user send illegal message.
                            
                            close(fdlist[i]);
                            int index = i;
                            while (index < fdcount){
                                fdlist[index] = fdlist[index+1];
                                userlist[index] = userlist[index+1];
                                index++;
                            }
                            fdcount--;
                        }

                    }
                }
            }
        }
    }
    exit(0);
}
