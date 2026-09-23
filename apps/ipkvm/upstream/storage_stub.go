// Stub for storage/SD functions removed

package kvm

import "github.com/gin-gonic/gin"

func handleUploadHttp(c *gin.Context) {
	c.JSON(404, gin.H{"error": "not supported"})
}

func handleDownloadHttp(c *gin.Context) {
	c.JSON(404, gin.H{"error": "not supported"})
}

func handleSDDownloadHttp(c *gin.Context) {
	c.JSON(404, gin.H{"error": "not supported"})
}
