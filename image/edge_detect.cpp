/*
 * @file    edge_detect.cpp
 * @brief   Sobel/Canny边缘检测实现
 *
 */


#include "edge_detect.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>



/*
 * Sobel检测
 */
int sobel_detect(
        uint8_t *gray,
        uint8_t *edge,
        int width,
        int height,
        int threshold)
{

    if(gray == NULL ||
       edge == NULL)
    {
        return -1;
    }



    memset(
        edge,
        0,
        width * height
    );



    for(int y = 1;
        y < height - 1;
        y++)
    {

        for(int x = 1;
            x < width - 1;
            x++)
        {


            int gx =
                -gray[(y-1)*width+x-1]
                -2*gray[y*width+x-1]
                -gray[(y+1)*width+x-1]
                +gray[(y-1)*width+x+1]
                +2*gray[y*width+x+1]
                +gray[(y+1)*width+x+1];



            int gy =
                -gray[(y-1)*width+x-1]
                -2*gray[(y-1)*width+x]
                -gray[(y-1)*width+x+1]
                +gray[(y+1)*width+x-1]
                +2*gray[(y+1)*width+x]
                +gray[(y+1)*width+x+1];



            int magnitude =
                abs(gx)+abs(gy);



            if(magnitude > threshold)
            {
                edge[y*width+x]=255;
            }
            else
            {
                edge[y*width+x]=0;
            }

        }

    }


    return 0;
}






/*
 * 简化版Canny
 *
 * 当前实现：
 *
 * 高斯滤波省略
 *
 * 采用梯度+双阈值
 *
 */
int canny_detect(
        uint8_t *gray,
        uint8_t *edge,
        int width,
        int height,
        int low_threshold,
        int high_threshold)
{

    if(gray == NULL ||
       edge == NULL)
    {
        return -1;
    }



    memset(
        edge,
        0,
        width*height
    );



    for(int y=1;
        y<height-1;
        y++)
    {

        for(int x=1;
            x<width-1;
            x++)
        {


            int gx =
                -gray[(y-1)*width+x-1]
                +gray[(y-1)*width+x+1]
                -2*gray[y*width+x-1]
                +2*gray[y*width+x+1]
                -gray[(y+1)*width+x-1]
                +gray[(y+1)*width+x+1];



            int gy =
                -gray[(y-1)*width+x-1]
                -2*gray[(y-1)*width+x]
                -gray[(y-1)*width+x+1]
                +gray[(y+1)*width+x-1]
                +2*gray[(y+1)*width+x]
                +gray[(y+1)*width+x+1];



            int magnitude =
                sqrt(
                    gx*gx+
                    gy*gy
                );



            if(magnitude > high_threshold)
            {
                edge[y*width+x]=255;
            }
            else if(magnitude > low_threshold)
            {
                edge[y*width+x]=128;
            }
            else
            {
                edge[y*width+x]=0;
            }

        }
    }



    return 0;
}