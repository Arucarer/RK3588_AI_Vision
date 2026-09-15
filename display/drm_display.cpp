/*
 * @file    drm_display.cpp
 * @brief   RK3588 DRM HDMI显示底层驱动
 */

#include "drm_display.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include <xf86drm.h>
#include <xf86drmMode.h>



#define DRM_DEVICE "/dev/dri/card0"


#define HDMI_CONNECTOR_ID 208
#define CRTC_ID           71


#define DISPLAY_WIDTH 1920
#define DISPLAY_HEIGHT 1080



static int drm_fd = -1;


static uint32_t fb_id;


static void *framebuffer = NULL;


static uint32_t handle;


static uint32_t pitch;



static int create_dumb_buffer()
{


    struct drm_mode_create_dumb create;


    memset(
        &create,
        0,
        sizeof(create)
    );



    create.width =
        DISPLAY_WIDTH;


    create.height =
        DISPLAY_HEIGHT;


    create.bpp = 32;



    if(
        ioctl(
            drm_fd,
            DRM_IOCTL_MODE_CREATE_DUMB,
            &create
        ) < 0
    )
    {

        perror(
            "create dumb"
        );


        return -1;

    }



    handle =
        create.handle;


    pitch =
        create.pitch;



    struct drm_mode_map_dumb map;


    memset(
        &map,
        0,
        sizeof(map)
    );



    map.handle =
        handle;



    ioctl(
        drm_fd,
        DRM_IOCTL_MODE_MAP_DUMB,
        &map
    );



    framebuffer =
        mmap(
            NULL,
            create.size,
            PROT_READ |
            PROT_WRITE,
            MAP_SHARED,
            drm_fd,
            map.offset
        );



    if(framebuffer==MAP_FAILED)
    {

        perror(
            "mmap"
        );


        framebuffer=NULL;


        return -1;

    }



    if(
        drmModeAddFB(
            drm_fd,
            DISPLAY_WIDTH,
            DISPLAY_HEIGHT,
            24,
            32,
            pitch,
            handle,
            &fb_id
        )
    )
    {

        perror(
            "add fb"
        );


        return -1;

    }



    return 0;

}






int drm_display_init()
{


    drm_fd =
        open(
            DRM_DEVICE,
            O_RDWR
        );


    if(drm_fd < 0)
    {

        perror(
            "open drm"
        );


        return -1;

    }



    printf(
        "DRM open success\n"
    );



    if(create_dumb_buffer()!=0)
    {

        return -1;

    }



    uint32_t connector_id = HDMI_CONNECTOR_ID;


    /*
    * 获取connector当前显示模式
    */
    drmModeConnector *connector;


    connector =
        drmModeGetConnector(
            drm_fd,
            connector_id
        );


    if(connector == NULL)
    {
        perror(
            "get connector"
        );

        return -1;
    }



    if(connector->count_modes <= 0)
    {
        printf(
            "no display mode\n"
        );

        drmModeFreeConnector(
            connector
        );

        return -1;
    }



    /*
    * 查找1920x1080@60模式
    *
    * 当前HDMI实际支持模式
    */
    drmModeModeInfo mode;

    int mode_found = 0;


    for(int i = 0;
        i < connector->count_modes;
        i++)
    {

        printf(
            "mode[%d]: name=%s width=%d height=%d refresh=%d\n",
            i,
            connector->modes[i].name,
            connector->modes[i].hdisplay,
            connector->modes[i].vdisplay,
            connector->modes[i].vrefresh
        );


        if(connector->modes[i].hdisplay == 1920 &&
        connector->modes[i].vdisplay == 1080 &&
        connector->modes[i].vrefresh == 60)
        {

            mode =
                connector->modes[i];


            mode_found = 1;


            printf(
                "select 1920x1080@60 mode\n"
            );


            break;

        }

    }



    if(!mode_found)
    {

        printf(
            "1920x1080@60 mode not found\n"
        );


        drmModeFreeConnector(
            connector
        );


        return -1;

    }



    printf(
        "DRM select mode %s %dx%d@%d\n",
        mode.name,
        mode.hdisplay,
        mode.vdisplay,
        mode.vrefresh
    );


    if(
        drmModeSetCrtc(
            drm_fd,
            CRTC_ID,
            fb_id,
            0,
            0,
            &connector_id,
            1,
            &mode
        )
    )
    {

        perror(
            "set crtc"
        );


        drmModeFreeConnector(
            connector
        );


        return -1;

    }



    drmModeFreeConnector(
        connector
    );



    printf(
        "DRM HDMI display start\n"
    );



    return 0;

}








int drm_display_frame(
        uint8_t *rgb,
        int width,
        int height
)
{


    if(rgb==NULL ||
       framebuffer==NULL)
    {

        return -1;

    }



    for(
        int y=0;
        y<DISPLAY_HEIGHT;
        y++
    )
    {


        for(
            int x=0;
            x<DISPLAY_WIDTH;
            x++
        )
        {


            int src =
                (y*width+x)*3;



            int dst =
                y*pitch+x*4;



            ((uint8_t*)framebuffer)[dst+0]
                = rgb[src+2];


            ((uint8_t*)framebuffer)[dst+1]
                = rgb[src+1];


            ((uint8_t*)framebuffer)[dst+2]
                = rgb[src+0];


            ((uint8_t*)framebuffer)[dst+3]
                = 0xff;


        }

    }



    return 0;

}








void drm_display_deinit()
{


    if(framebuffer)
    {


        munmap(
            framebuffer,
            DISPLAY_WIDTH *
            DISPLAY_HEIGHT *
            4
        );


        framebuffer=NULL;

    }




    if(fb_id)
    {


        drmModeRmFB(
            drm_fd,
            fb_id
        );


        fb_id=0;

    }




    if(drm_fd>=0)
    {

        close(
            drm_fd
        );


        drm_fd=-1;

    }


}