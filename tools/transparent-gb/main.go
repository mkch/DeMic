// This program converts BMP images on a white background (R: 255, G: 255, B: 255)
// containing only pure black (0, 0, 0) and pure red (255, 0, 0) into PNG images with an alpha channel.
// Since the original images use antialiasing on object edges, the alpha channel is calculated
// based on the G channel values, and the R channel is binarized to ensure sharp edges.
package main

import (
	"flag"
	"image"
	"image/color"
	"image/png"
	"log"
	"os"

	_ "golang.org/x/image/bmp"
)

func main() {
	flag.Parse()
	for _, src := range flag.Args() {
		dst := src + "-out.png"
		processFile(src, dst)
	}
}

func processFile(inputPath, outputPath string) {
	inputFile, err := os.Open(inputPath)
	if err != nil {
		log.Fatal(err)
	}
	defer inputFile.Close()

	img, _, err := image.Decode(inputFile)
	if err != nil {
		log.Fatal(err)
	}

	bounds := img.Bounds()
	// Create a new image with NRGBA format to hold the processed pixels
	newImg := image.NewNRGBA(bounds)

	for y := bounds.Min.Y; y < bounds.Max.Y; y++ {
		for x := bounds.Min.X; x < bounds.Max.X; x++ {
			// R, G, B, A (0-65535)
			r, g, _, _ := img.At(x, y).RGBA()
			// Calculate Alpha (based on G channel, as G is always 0 in object color)
			alpha := 1.0 - (float64(g) / 65535.0)
			var finalColor color.Color
			if alpha <= 0 {
				// Totally transparent
				finalColor = color.Transparent
			} else {
				finalColor = color.NRGBA64{
					R: uint16(float64(r) / alpha), // Restore R based on alpha
					G: 0,
					B: 0,
					A: uint16(alpha * 65535.0),
				}
			}
			newImg.Set(x, y, finalColor)
		}
	}

	outputFile, err := os.OpenFile(outputPath, os.O_WRONLY|os.O_CREATE|os.O_EXCL, 0666)
	if err != nil {
		log.Fatal(err)
	}
	defer outputFile.Close()

	err = png.Encode(outputFile, newImg)
	if err != nil {
		log.Fatal(err)
	}
}
