#include <netinet/in.h>
#include <linux/vm_sockets.h>
#include <arpa/inet.h>
#include <dlfcn.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <map>

#define CID 4 // What CID do we bind on? Can we do this via an env variable?
//#define TEST_INET // For testing. if enabled, bind a second INET port instead of a VSOCK
#define FD_OFFSET 100 // When guest makes a new socket, we want to keep the original FD somewhere,
// we'll shift by this much so subsequent FD nubmers behave as normal even though it shouldn't matter


// Overall idea: Guest can create AF_INET sockets, but if it tries to bind on one,
// we'll bind on an AF_VSOCK instead. We'll use some tricks so this is largely
// unnoticed in the guest, but we're not trying to be stealthy and it could
// deinitely be detected if a guest looks closely at the network connections 
// and FDs (e.g, in proc)
//
// Implemented by hooking calls to socket() such that we create both an AF_INET
// with the requested options AND an AF_VSOCK object. We return the FD for the VSOCK,
// and pass some syscalls (getsockname) to the original INET FD so everything looks right
// When the guest goes to bind on the FD it thinks is an INET FD, it will actually
// bind on our VSOCK.
//
// Up until a connection is established, we just need to hook socket to create and return the vsock fd,
// and how getsockname, getsockopt, setsockopt read and write metadata about the INET socket.
//
// When the guest binds() on the VSOCK fd, we'll also bind on the original FD. This will allow
// for local connections to the original service, but more importantly we'll detect normal errors (and bailx
// early) and be able to query state of the original FD later to get normal results. Then we bind the VSOCK
// and return the result of the INET bind

// Approach is based off the common syscall pattern of something like:
// s = socket(AF_INET, ...)
// setsocktop(s)
// bind(s, {sa_family=AF_INET
// getsockname(s, {sa_family=AF_INET
// listen(s, backlog_size)
//
// getsockname(s, {sa_family=AF_INET
// poll(s
// ...
// s' = accept(4){s, sa_family=AF_INET,
// getsockname(s', sa_family

// On bind, we return 
// listen: if it's ours, listen on the VSOCK, not the INET
// poll: if it's ours, listen on the VSOCK
// accept/accept4: if it's ours

std::map<int, int> v2i; // vsock to INET file descriptor mapping
std::map<int, int> l2v; // accepted fd ("listening" then got dat) mapped to vsock

extern "C" { // Probably unnecessary?
int (*original_socket)(int, int, int); // Create originally requested socket store in v2i, make VSOCK, return V

int (*original_getsockname)(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int (*original_getpeername)(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int (*original_listen)(int sockfd, int backlog);

int (*original_bind)(int sockfd, const struct sockaddr *, socklen_t); // Bind both INET and VSOCK
//int (*original_accept)(int sockfd, struct sockaddr *, socklen_t *);
int (*original_accept4)(int sockfd, struct sockaddr *addr, socklen_t *addrlen, int flags);

int (*original_close)(int fd);
int (*original_shutdown)(int sockfd, int how);


int (*original_getsockopt)(int sockfd, int level, int optname, void *optval, socklen_t *optlen);
int (*original_setsockopt)(int sockfd, int level, int optname, void *optval, socklen_t *optlen);
}

__attribute__((constructor)) void get_orig() {
	original_socket =      (int(*)(int, int, int)) dlsym(RTLD_NEXT, "socket");

	original_getsockname = (int (*)(int, sockaddr*, socklen_t*))dlsym(RTLD_NEXT, "getsockname");
	original_getpeername = (int (*)(int, sockaddr*, socklen_t*))dlsym(RTLD_NEXT, "getpeername");
	original_listen =      (int (*)(int, int))dlsym(RTLD_NEXT, "listen");

	original_bind =        (int (*)(int, const sockaddr*, socklen_t)) dlsym(RTLD_NEXT, "bind");
	//original_accept =      (int (*)(int, sockaddr*, socklen_t*)) dlsym(RTLD_NEXT, "accept");
	original_accept4 =     (int (*)(int, sockaddr*, socklen_t*, int))dlsym(RTLD_NEXT, "accept4");

	original_getsockopt =  (int (*)(int, int, int, void*, socklen_t*))dlsym(RTLD_NEXT, "getsockopt");
	original_setsockopt =  (int (*)(int, int, int, void*, socklen_t*))dlsym(RTLD_NEXT, "setsockopt");

	original_close =       (int (*)(int))dlsym(RTLD_NEXT, "close");
  original_shutdown =    (int (*)(int, int))dlsym(RTLD_NEXT, "shutdown");
}

int socket(int domain, int type, int protocol) {
	if (domain != AF_INET ) {
    // Ignore ipv6 for now
		return original_socket(domain, type, protocol);
	}

  int new_s =
#ifdef TEST_INET
  original_socket(AF_INET, type, 0); // Just request the exact same socket type, we'll shift ports later
#else
  original_socket(AF_VSOCK, type, 0);
  if (new_s < 0) {
    // Couldn't create with requested type: let's just do SOCK_STREAM. Maybe we should always ignore type?
    new_s = original_socket(AF_VSOCK, SOCK_STREAM, 0);
  }
#endif
  int org_s = original_socket(domain, type, protocol);

  if (new_s < 0) {
    printf("VSockify failure, new socket got errno %d. Return original socket\n", new_s);
    return org_s;
  }

  // Some applications could make bad assumptions about subsequent FD numbers
  // let's shift the 2nd (the original requested fd) to be a bit higher
  dup2(org_s, org_s+FD_OFFSET-1);
  close(org_s);
  org_s += FD_OFFSET-1;

  v2i[new_s] = org_s;

  printf("Original socket %d with VSOCK %d\n", org_s, new_s);
  return new_s;
}

int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
  std::map<int, int>::iterator org_fd_iter = v2i.find(sockfd);
  if (org_fd_iter == v2i.end()) {
    // If it's not one of ours, just bind and be done
    return original_bind(sockfd, addr, addrlen);
  }

  // If it's one of ours, get the original result. If that fails, return early
  int org_rv = original_bind(v2i[sockfd], addr, addrlen);
  if (org_rv < 0) {
    // Failure - This is bad, the original socket worked but ours didn't, we should tell the user
    printf("VSOCKIFY failure binding our socket: %d\n", org_rv);
    // Propagate the error to the guest so we really cause problems
    return org_rv;
  }

  unsigned short portno = ntohs(((struct sockaddr_in*)addr)->sin_port);

  // Now let's bind our second socket. Note we *already* bound the actual socket
#ifdef TEST_INET
  struct sockaddr_in new_addr = {0};
  printf("Doing an IPv4 bind on port %u as offset from actual %u\n", portno+100, portno);
  new_addr.sin_family = AF_INET;
  new_addr.sin_port = htons(portno + 100);
  inet_aton("0.0.0.0", &new_addr.sin_addr);
#else
  struct sockaddr_vm new_addr = {0};
  printf("Doing a vsock bind on port %u to match the actual port\n", portno);
  new_addr.svm_family = AF_VSOCK;
  new_addr.svm_port = portno; // host byte order (should be okay?) XXX must be > 1024 if non-root
  new_addr.svm_cid = (-1U); // bind *any* CID - if we're in a VM we should just have a single CID
#endif

  // Ignore the original RV, return the result of binding the vsock which is a new, VSOCK-based FD
  int new_fd = original_bind(sockfd, (struct sockaddr*)&new_addr, sizeof(new_addr));

  // store that the new FD is backed by the socket we created
  l2v[new_fd] = new_fd;

  return new_fd;
}

int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
  return accept4(sockfd, addr, addrlen, 0);
}

int accept4(int sockfd, struct sockaddr *addr, socklen_t *addrlen, int flags) {
  std::map<int, int>::iterator org_fd_iter = v2i.find(sockfd);
  if (org_fd_iter == v2i.end()) {
    // Not one of ours, pass through
    return original_accept4(sockfd, addr, addrlen, flags);
  }

  // We need to return a sockaddr describing the "peer connection"
  if (addrlen == NULL) return -EINVAL;
  if (addr != NULL) {
    if (*addrlen < sizeof(struct sockaddr_in)) {
      *addrlen = sizeof(struct sockaddr_in);
    } else {
      // Port 0, ip 0? I guess?
      ((struct sockaddr_in*)addr)->sin_family = AF_INET;
      ((struct sockaddr_in*)addr)->sin_port = htons(6000); // XXX not sure what to really put here, but probably not 0?
      inet_aton("127.0.0.1", &((struct sockaddr_in*)addr)->sin_addr);
    }
  }

  // It's one of ours, so let's accept on it, then store the listening fd in l2v
  int new_fd = original_accept4(sockfd, addr, addrlen, flags); // XXX addr info?
  l2v[new_fd] = sockfd;
  return new_fd;
}

int shutdown(int sockfd, int how) {
  std::map<int, int>::iterator org_fd_iter = v2i.find(sockfd);

  // Shut down socket regardless of if its ours or not
  int rv = original_shutdown(sockfd, how);

  if (org_fd_iter != v2i.end()) {
    // If it was ours, let's also shutdown the AF_INET and get that RV to return
    rv = original_shutdown(v2i[sockfd], how);

    // Cleanup our state. Drop from v2i, and any stale references in l2v.
    v2i.erase(sockfd);
    // Note we don't need to close these ourself, that's somethign the kernel should handle
    for (auto it : l2v) { 
      if (it.second == sockfd) { 
        l2v.erase(it.first);
      } 
    }
  }

  // Return result of shutting down original socket, or the VSOCK if we had one
  return rv;
}

int getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen) {
  // This would run on a non-listening socket
  std::map<int, int>::iterator org_fd_iter = v2i.find(sockfd);
  if (org_fd_iter == v2i.end()) {
    // if it's one of ours update the local sockfd var to be the original fd
    sockfd = v2i[sockfd];
  }

  // Get option from the original socket
  return getsockopt(sockfd, level, optname, optval, optlen);
}

int setsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen) {
  // This would run on a non-listening socket
  std::map<int, int>::iterator org_fd_iter = v2i.find(sockfd);
  if (org_fd_iter != v2i.end()) {
    // if it's one of ours update the local sockfd var to be the original fd
    sockfd = v2i[sockfd];
  }

  // TODO: are there any options we want to also apply to vsocks: probably nonblocking?
  return setsockopt(sockfd, level, optname, optval, optlen);
}

int getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
  // Return details of the originally requested socket that bound
  std::map<int, int>::iterator org_fd_iter = v2i.find(sockfd);
  if (org_fd_iter != v2i.end()) {
    // if it's one of ours update the local sockfd var to be the original fd
    sockfd = v2i[sockfd];
  }
  return original_getsockname(sockfd, addr, addrlen);
}

int getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
  std::map<int, int>::iterator org_fd_iter = v2i.find(sockfd);
  if (org_fd_iter == v2i.end()) {
    // Not one of ours
    return original_getpeername(sockfd, addr, addrlen);
  }

  // This one is tricky. We don't actually have a peer address. So we're gonna lie through our teeth.
  // Just say it's a connection from localhost
  // The man page makes me think we can call this and get some bogus results before connect()
  // so maybe we just try it?
  return original_getpeername(v2i[sockfd], addr, addrlen);
}

// Guest should never get to listen on the original FD if we changed it. Just a sanity check
int listen(int sockfd, int backlog) {
  std::map<int, int>::iterator org_fd_iter = v2i.find(sockfd);
  for (auto &it : v2i) { 
    if (it.second == sockfd) { 
      printf("VSOCKIFY ERROR: Guest listens on INET FD %d instead of our FD %d\n", sockfd, it.first);
      return -EBADF;
    } 
  }
  return original_listen(sockfd, backlog);
}
