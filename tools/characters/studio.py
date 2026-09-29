#!/usr/bin/env python3
"""Local character sliders using the same JSON builder and native Scener renderer."""
import argparse
import base64
from datetime import datetime
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import threading
import sys

from build_character import build,validate,DEFAULTS,BASES,reference,source_metrics,REPO
from ecstatica_convert import options
from ecstatica_convert import slug

HTML=Path(__file__).with_name('studio.html')
CAMERAS=('Standing','Front','Side','Portrait')


def handler(output,scener,port):
    lock=threading.Lock();source_cache={}
    def render(spec,camera):
        with tempfile.TemporaryDirectory(prefix='character-preview-') as temp:
            temp=Path(temp);build(spec,temp/'model')
            result=subprocess.run([scener,'--render',str(temp/'model/scenes/study.blks'),'--camera',camera,'--size','720x720','--format','png','--output-dir',str(temp/'render')],capture_output=True,text=True,timeout=60)
            if result.returncode:raise RuntimeError('Scener preview failed: '+result.stderr[-1600:])
            return 'data:image/png;base64,'+base64.b64encode((temp/'render'/f'{camera}.png').read_bytes()).decode()

    class Handler(BaseHTTPRequestHandler):
        def log_message(self,fmt,*args):print('[character-studio] '+fmt%args,file=sys.stderr,flush=True)
        def reply(self,status,value,content_type='application/json'):
            data=json.dumps(value).encode() if content_type=='application/json' else value
            self.send_response(status);self.send_header('Content-Type',content_type)
            self.send_header('Content-Length',str(len(data)));self.send_header('Cache-Control','no-store')
            self.send_header('X-Content-Type-Options','nosniff');self.end_headers();self.wfile.write(data)
        def local_request(self):
            allowed={f'127.0.0.1:{port}',f'localhost:{port}'}
            return self.headers.get('Host') in allowed and (not self.headers.get('Origin') or self.headers['Origin'] in {'http://'+x for x in allowed})
        def do_GET(self):
            if not self.local_request():return self.reply(403,{'error':'Local requests only'})
            if self.path=='/':return self.reply(200,HTML.read_bytes(),'text/html; charset=utf-8')
            if self.path=='/catalog':
                return self.reply(200,{'bases':{key:{'label':label,'height_heads':source_metrics(key)['heads'],'features':list(options(reference(key),{}))} for key,label in BASES.items()},'defaults':DEFAULTS})
            return self.reply(404,{'error':'Not found'})
        def do_POST(self):
            if not self.local_request():return self.reply(403,{'error':'Local requests only'})
            if self.path not in ('/preview','/export'):return self.reply(404,{'error':'Not found'})
            if self.headers.get('Content-Type','').split(';')[0]!='application/json':return self.reply(415,{'error':'JSON required'})
            try:
                size=int(self.headers.get('Content-Length','0'))
                if not 0<size<=32768:raise ValueError('Recipe request too large or empty')
                data=json.loads(self.rfile.read(size));spec=validate(data['recipe']);camera=data.get('camera','Standing')
                if camera not in CAMERAS:raise ValueError('Invalid camera')
                self.log_message('%s camera=%s recipe=%s',self.path,camera,json.dumps(spec,sort_keys=True))
                with lock:
                    if self.path=='/export':
                        dest=output/(datetime.now().strftime('%Y%m%d-%H%M%S-%f')+'-'+slug(spec['name']))
                        build(spec,dest);return self.reply(200,{'folder':str(dest)})
                    response={'image':render(spec,camera)}
                    if data.get('compare',False):
                        key=(spec['base'],camera)
                        if key not in source_cache:source_cache[key]=render(DEFAULTS|{'base':spec['base']},camera)
                        response['source_image']=source_cache[key]
                    self.reply(200,response)
            except (ValueError,KeyError,TypeError,OSError,RuntimeError,subprocess.TimeoutExpired) as error:
                self.log_message('rejected %s: %s',self.path,error);self.reply(400,{'error':str(error)})
    return Handler


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--scener',default=shutil.which('scener') or str(Path.home()/'.local/bin/scener'))
    p.add_argument('--port',type=int,default=8765);args=p.parse_args()
    args.output.mkdir(parents=True,exist_ok=True)
    server=ThreadingHTTPServer(('127.0.0.1',args.port),handler(args.output.resolve(),args.scener,args.port))
    print(f'[character-studio] http://127.0.0.1:{args.port} exports={args.output.resolve()}',flush=True)
    try:server.serve_forever()
    except KeyboardInterrupt:pass
    finally:server.server_close()


if __name__=='__main__':main()
