          .386

IO_TEXT   segment   word public 'CODE'
          assume    cs:IO_TEXT

          public    WPORT
WPORT     proc      far

          push bp
          mov  bp,sp
          push ax
          push dx

          mov  ax,[bp+6]
          mov  dx,[bp+8]
          out  dx,al

          pop  dx
          pop  ax
          pop  bp

          ret  4

WPORT     endp

          public    WPORT16
WPORT16   proc      far

          push bp
          mov  bp,sp
          push ax
          push dx

          mov  ax,[bp+6]
          mov  dx,[bp+8]
          out  dx,ax

          pop  dx
          pop  ax
          pop  bp

          ret  4

WPORT16   endp

          public    RPORT
RPORT     proc      far

          push bp
          mov  bp,sp
          push dx

          mov  dx,[bp+6]
          in   al,dx
          xor  ah,ah

          pop  dx
          pop  bp

          ret  2

RPORT     endp


          public    RPORT16
RPORT16   proc      far

          push bp
          mov  bp,sp
          push dx

          mov  dx,[bp+6]
          in   ax,dx

          pop  dx
          pop  bp

          ret  2

RPORT16   endp

          public    RPORTN
RPORTN    proc      far

          push bp
          mov  bp,sp
          push es
          push di
          push dx

          mov  dx,[bp+12]
          les  di,[bp+8]
          mov  cx,[bp+6]
          rep insb

          pop  dx
          pop  di
          pop  es
          pop  bp

          ret  8

RPORTN    endp


          public    RPORTN16
RPORTN16  proc      far

          push bp
          mov  bp,sp
          push es
          push di
          push dx

          mov  dx,[bp+6]
          les  di,[bp+8]
          mov  cx,[bp+12]
          rep insw

          pop  dx
          pop  di
          pop  es
          pop  bp

          ret  8

RPORTN16  endp

IO_TEXT   ends

          end
