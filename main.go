package main

import (
	"flag"
	"fmt"
	"log"
	"mime"
	"net/http"
	"os"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"
)

type VideoServer struct {
	basePath string
}

func (s *VideoServer) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	// Log request
	fmt.Printf("\n%s\n", strings.Repeat("=", 50))
	fmt.Printf("📥 REQUEST\n")
	fmt.Printf("%s\n", strings.Repeat("=", 50))
	fmt.Printf("Method: %s\n", r.Method)
	fmt.Printf("Path: %s\n\n", r.URL.Path)
	fmt.Printf("Headers:\n")
	for k, v := range r.Header {
		fmt.Printf("  %s: %s\n", k, v[0])
	}
	fmt.Printf("%s\n\n", strings.Repeat("=", 50))

	if r.Method == "OPTIONS" {
		w.Header().Set("Allow", "GET, HEAD, OPTIONS")
		w.WriteHeader(http.StatusNoContent)
		return
	}

	filePath := s.translatePath(r.URL.Path)
	fmt.Printf("filepath: %s\n", filePath)

	fileInfo, err := os.Stat(filePath)
	if err != nil {
		http.Error(w, "File not found", http.StatusNotFound)
		return
	}

	if fileInfo.IsDir() {
		http.Error(w, "Directories not supported", http.StatusNotFound)
		return
	}

	contentType := mime.TypeByExtension(filepath.Ext(filePath))
	if contentType == "" {
		contentType = "application/octet-stream"
	}

	fileSize := fileInfo.Size()

	if rangeHeader := r.Header.Get("Range"); rangeHeader != "" {
		s.handleRangeRequest(w, r, filePath, rangeHeader, fileSize, contentType)
	} else {
		s.serveFullFile(w, filePath, fileSize, contentType)
	}
}

func (s *VideoServer) handleRangeRequest(w http.ResponseWriter, r *http.Request, filePath string, rangeHeader string, fileSize int64, contentType string) {
	re := regexp.MustCompile(`bytes=(\d*)-(\d*)`)
	matches := re.FindStringSubmatch(rangeHeader)
	if matches == nil {
		http.Error(w, "Invalid Range header", http.StatusBadRequest)
		return
	}

	start := int64(0)
	if matches[1] != "" {
		start, _ = strconv.ParseInt(matches[1], 10, 64)
	}

	end := fileSize - 1
	if matches[2] != "" {
		end, _ = strconv.ParseInt(matches[2], 10, 64)
	}

	if start > end || start >= fileSize {
		w.Header().Set("Content-Range", fmt.Sprintf("bytes */%d", fileSize))
		http.Error(w, "Requested range not satisfiable", http.StatusRequestedRangeNotSatisfiable)
		return
	}

	length := end - start + 1

	w.Header().Set("Content-Type", contentType)
	w.Header().Set("Content-Length", strconv.FormatInt(length, 10))
	w.Header().Set("Content-Range", fmt.Sprintf("bytes %d-%d/%d", start, end, fileSize))
	w.Header().Set("Accept-Ranges", "bytes")
	w.WriteHeader(http.StatusPartialContent)

	file, err := os.Open(filePath)
	if err != nil {
		http.Error(w, "Failed to open file", http.StatusInternalServerError)
		return
	}
	defer file.Close()

	file.Seek(start, 0)
	bytesSent := int64(0)
	buffer := make([]byte, 8192) // Same 8KB chunks as Python

	for bytesSent < length {
		toRead := min(8192, length-bytesSent)
		n, err := file.Read(buffer[:toRead])
		if err != nil || n == 0 {
			break
		}
		w.Write(buffer[:n])
		bytesSent += int64(n)
	}

	// Log response
	fmt.Printf("\n%s\n", strings.Repeat("=", 50))
	fmt.Printf("📤 RESPONSE\n")
	fmt.Printf("%s\n", strings.Repeat("=", 50))
	fmt.Printf("Status: %d\n\n", http.StatusPartialContent)
	fmt.Printf("Headers:\n")
	for k, v := range w.Header() {
		fmt.Printf("  %s: %s\n", k, v[0])
	}
	fmt.Printf("Body size: %d bytes\n", bytesSent)
	fmt.Printf("%s\n\n", strings.Repeat("=", 50))
}

func (s *VideoServer) serveFullFile(w http.ResponseWriter, filePath string, fileSize int64, contentType string) {
	w.Header().Set("Content-Type", contentType)
	w.Header().Set("Content-Length", strconv.FormatInt(fileSize, 10))
	w.Header().Set("Accept-Ranges", "bytes")

	file, err := os.Open(filePath)
	if err != nil {
		http.Error(w, "Failed to open file", http.StatusInternalServerError)
		return
	}
	defer file.Close()

	buffer := make([]byte, 8192)
	bytesSent := int64(0)

	for {
		n, err := file.Read(buffer)
		if err != nil || n == 0 {
			break
		}
		w.Write(buffer[:n])
		bytesSent += int64(n)
	}

	// Log response
	fmt.Printf("\n%s\n", strings.Repeat("=", 50))
	fmt.Printf("📤 RESPONSE\n")
	fmt.Printf("%s\n", strings.Repeat("=", 50))
	fmt.Printf("Status: %d\n\n", http.StatusOK)
	fmt.Printf("Headers:\n")
	for k, v := range w.Header() {
		fmt.Printf("  %s: %s\n", k, v[0])
	}
	fmt.Printf("Body size: %d bytes\n", bytesSent)
	fmt.Printf("%s\n\n", strings.Repeat("=", 50))
}

func (s *VideoServer) translatePath(path string) string {
	path = filepath.Clean(path)
	if strings.HasPrefix(path, "/") {
		path = path[1:]
	}
	return filepath.Join(s.basePath, path)
}

func min(a, b int64) int64 {
	if a < b {
		return a
	}
	return b
}

func main() {
	port := flag.Int("port", 8080, "Port to serve on")
	basePath := flag.String("path", "/videos", "Base path for video files")
	flag.Parse()

	server := &VideoServer{
		basePath: *basePath,
	}

	absPath, _ := filepath.Abs(*basePath)
	fmt.Printf("Serving videos from %s on port %d\n", absPath, *port)
	fmt.Printf("Access videos at http://localhost:%d/\n", *port)

	http.Handle("/", server)
	log.Fatal(http.ListenAndServe(fmt.Sprintf(":%d", *port), nil))
}