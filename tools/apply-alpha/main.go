// Command apply-alpha applies a specified alpha percentage to each pixel of an image.
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
	alpha := flag.Int("alpha", 0, "The alpha percentage to apply.")
	flag.Parse()
	for _, src := range flag.Args() {
		dst := src + "-out.png"
		processFile(src, dst, *alpha)
	}
}

func processFile(inputPath, outputPath string, alphaPercentage int) {
	if alphaPercentage < 0 || alphaPercentage > 100 {
		log.Fatal("Alpha percentage must be between 0 and 100")
	}

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
	// Create a new image with RGBA format to hold the processed pixels
	newImg := image.NewRGBA(bounds)

	var scale = func(c uint32, percentage uint32) uint32 {
		return uint32((uint64(c) * uint64(percentage) / 100))
	}
	for y := bounds.Min.Y; y < bounds.Max.Y; y++ {
		for x := bounds.Min.X; x < bounds.Max.X; x++ {
			// R, G, B, A (0-65535)
			r, g, b, a := img.At(x, y).RGBA()
			newImg.Set(x, y, color.RGBA{
				R: uint8(scale(r, uint32(alphaPercentage)) >> 8), // Convert from 16-bit to 8-bit
				G: uint8(scale(g, uint32(alphaPercentage)) >> 8),
				B: uint8(scale(b, uint32(alphaPercentage)) >> 8),
				A: uint8(scale(a, uint32(alphaPercentage)) >> 8),
			})
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
