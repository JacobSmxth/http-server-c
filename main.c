#include <arpa/inet.h>
#include <dirent.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <regex.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define PORT 8080
#define BUFFER_SIZE 104857600 // Temporary storage for processing data


const char *get_file_extension(const char *file_name);
const char *get_mime_type(const char *file_ext);
char *url_decode(const char *src);
void build_http_response(const char *file_name, const char *file_ext, char *response, size_t *response_len);
void *handle_client(void *arg);


int main () {
  int server_fd;
  struct sockaddr_in server_addr;

  if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) { // This creates a TCP socket and tells is to listen to all interfaces on a specified port
    perror("socket failed");
    exit(EXIT_FAILURE);
  }

  // configuration
  server_addr.sin_family = AF_INET; // This is just IPv4
  server_addr.sin_addr.s_addr = INADDR_ANY; // Binds to any connection from any IP
  server_addr.sin_port = htons(PORT); // converts port to network byte order and assings it to socket


  // bindsocket to port
  if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
    perror("bind failed");
    exit(EXIT_FAILURE);
  }

  // listen to port for incoming connections
  if (listen(server_fd, 10) < 0) {
    perror("listen failed");
    exit(EXIT_FAILURE);
  }
  printf("Server listening on port %d\n", PORT);

  // infinite loop of server to continue listening
  while (1) {
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    int *client_fd = malloc(sizeof(int));
    if (client_fd == NULL) {
      exit(EXIT_FAILURE);
    }


    // Accepting income client connection
    *client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
    if (*client_fd < 0) {
      perror("accept failed");
      continue;
    }

    // Handle multiple concurrent clients with threading
    pthread_t thread_id;
    if (pthread_create(&thread_id, NULL, handle_client, (void *)client_fd) != 0) {
      perror("pthread_create failed");
      close(*client_fd);
      exit(EXIT_FAILURE);
    }
    pthread_detach(thread_id);
  }
}



void *handle_client(void *arg) {
  int client_fd = *((int *)arg); // typecast back to int *
  char *buffer = (char *)malloc(BUFFER_SIZE * sizeof(char)); // Allocates a buffer for data
  if (buffer == NULL) {
    exit(EXIT_FAILURE);
  }

  // Recieve the request from client
  ssize_t bytes_recieved = recv(client_fd, buffer, BUFFER_SIZE, 0); // recv is system_call. Retunrs number of bytes recieved

  // Example Request:
  //
  // GET /hello.txt HTTP/1.1
  // Host: example.com
  // User-Agent: Mozilla/5.0
  // Accept: text/html,application/xhtml+xml,application/xml;q=0.9
  // Connection: keep-alive


  if (bytes_recieved > 0) { // If greater than 0 we recieved valid request

    // checking if request is a GET request
    regex_t regex;
    regcomp(&regex, "^GET /([^ ]*) HTTP/1", REG_EXTENDED);
    regmatch_t matches[2];

    // Check if matches our regex pattern
    if(regexec(&regex, buffer, 2, matches, 0) == 0) {

      buffer[matches[1].rm_eo] = '\0'; // This cuts everything after the filename off

      const char *url_encoded_file_name = buffer + matches[1].rm_so; // this now holds the filename part of the request

      char *file_name = url_decode(url_encoded_file_name);


      char file_ext[32];
      strcpy(file_ext, get_file_extension(file_name));

      char *response = (char *)malloc(BUFFER_SIZE * 2 * sizeof(char)); // Allocate memory for the response
      if (response == NULL) {
        exit(EXIT_FAILURE);
      }
      size_t response_len;
      build_http_response(file_name, file_ext, response, &response_len);

      send(client_fd, response, response_len, 0);

      free(response);
      free(file_name);
    }
    regfree(&regex);
  }
  close(client_fd);
  free(arg);
  free(buffer);
  return NULL;
}

void build_http_response(const char *file_name, const char *file_ext, char *response, size_t *response_len) {
  const char *mime_type = get_mime_type(file_ext);
  // Allocates memory for header and format HTTP response header
  char *header = (char *)malloc(BUFFER_SIZE * sizeof(char));
  if (header == NULL) {
    exit(EXIT_FAILURE);
  }
  snprintf(header, BUFFER_SIZE,
           "HTTP/1.1 200 OK\r\n"
           "Content-Type: %s\r\n"
           "\r\n",
           mime_type);


  // Try to open requested file
  // If fails build a 404 response
  int file_fd = open(file_name, O_RDONLY);
  if(file_fd == -1) {
    snprintf(response, BUFFER_SIZE,
             "HTTP/1.1 404 Not Found\r\n"
             "Content-Type: text/plain\r\n"
             "\r\n"
             "404 Not Found");
    *response_len = strlen(response);
    return;
  }

  // get file size for Content-Length
  struct stat file_stat;
  fstat(file_fd, &file_stat); // fstat gets file size before reading
  // off_t file_size = file_stat.st_size;

  // copy header to response buffer using memcpy
  *response_len = 0;
  memcpy(response, header, strlen(header));
  *response_len += strlen(header);

  ssize_t bytes_read;

  // Loop reads requested file and appends content to response buffer
  while((bytes_read = read(file_fd, response + *response_len, BUFFER_SIZE - *response_len)) > 0) { // Reads up to certain bytes, stores the data, read() returns the number of bytes read
    *response_len += bytes_read;

  }
  free(header);
  close(file_fd);
}

char *url_decode(const char *src) {
  size_t src_len = strlen(src);
  char *decoded = malloc(src_len + 1);
  if (decoded == NULL) {
    exit(EXIT_FAILURE);
  }
  size_t decoded_len = 0;

  for (size_t i = 0; i < src_len; i++) {
    if (src[i] == '%' && i + 2 < src_len) {
      int hex_val;
      sscanf(src + i + 1, "%2x", &hex_val);
      decoded[decoded_len++] = hex_val;
      i+= 2;
    } else {
      decoded[decoded_len++] = src[i];
    }
  }

  decoded[decoded_len] = '\0';
  return decoded;
}

const char *get_mime_type(const char *file_ext) {
  if (strcasecmp(file_ext, "html") == 0 || strcasecmp(file_ext, "htm") == 0) {
    return "text/html";
  } else if (strcasecmp(file_ext, "txt") == 0) {
    return "text/plain";
  } else if (strcasecmp(file_ext, "jpg") == 0 || strcasecmp(file_ext, "jpeg") == 0) {
    return "image/jpeg";
  } else if (strcasecmp(file_ext, "png") == 0) {
    return "image/png";
  } else {
    return "application/octet_stream";
  }
}

const char *get_file_extension(const char *file_name) {
  const char *dot = strrchr(file_name, '.');
  if (!dot || dot == file_name) {
    return "";
  }
  return dot + 1;
}
