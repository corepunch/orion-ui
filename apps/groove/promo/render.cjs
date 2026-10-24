// Render marketing layouts around unchanged captures of the real Groove UI.
const fs = require('node:fs/promises');
const path = require('node:path');
const sharp = require('sharp');

const root = __dirname;
const escape = s => String(s).replaceAll('&', '&amp;').replaceAll('<', '&lt;');
const names = ['skyline', 'afterglow', 'victory'];

async function main() {
  const captures = Object.fromEntries(await Promise.all(names.map(async name => [name,
    'data:image/jpeg;base64,' + (await fs.readFile(path.join(root, 'source', name + '.jpg'))).toString('base64')])));
  const icon = 'data:image/png;base64,' + (await fs.readFile(path.join(root, '../share/icon.png'))).toString('base64');
  const meta = await sharp(path.join(root, 'source/skyline.jpg')).metadata();
  const sets = [
    { name: 'macos', w: 2880, h: 1800, status: 'ready', target: 'Mac App Store' },
    { name: 'ipad-13-draft', w: 2752, h: 2064, status: 'awaiting-ipad-captures', target: 'iPad 13-inch' },
  ];
  const slides = [
    ['01-make-great-music', 'Make great music.'],
    ['02-find-your-sound', 'Find your sound.'],
    ['03-layer-your-ideas', 'Layer your ideas.'],
    ['04-editable-demos', 'A head start. Your own finish.'],
    ['05-keep-the-spark', 'Keep the spark.'],
  ];
  const manifest = { locale: 'en-US', source_platform: 'macOS', source_capture_size: [meta.width, meta.height],
    source_capture_command: 'build/bin/groove --demo=<skyline|afterglow|victory> --screenshot <path.png>',
    notes: ['Screens are real, unmodified app captures; enlarged details are cropped excerpts.',
      'Frames are presentation panels, not representations of Apple hardware.',
      'Replace source captures with actual iPad captures and rerender before uploading the iPad set.'],
    references: ['https://apps.apple.com/us/app/zillow-real-estate-rentals/id310738695',
      'https://developer.apple.com/help/app-store-connect/reference/app-information/screenshot-specifications'], sets: [] };

  for (const set of sets) {
    const {w: W, h: H} = set, X = W / 2752, Y = H / 2064, U = Math.min(X, Y);
    const out = path.join(root, set.name, 'en-US');
    await fs.mkdir(out, {recursive: true});
    const text = (value, x, y, size, color, weight = 700, anchor = 'start', tracking = -1) =>
      `<text x="${x*X}" y="${y*Y}" fill="${color}" font-family="Avenir Next,Arial,sans-serif" font-size="${size*U}" font-weight="${weight}" text-anchor="${anchor}" letter-spacing="${tracking*U}">${escape(value)}</text>`;
    const lines = (values, x, y, size, color, leading, weight = 700, anchor = 'start') =>
      values.map((s, i) => text(s, x, y + i*leading, size, color, weight, anchor)).join('');
    const pill = (label, x, y, color) => text(label, x, y, 35, color, 600, 'start', 6);
    const app = (name, box, crop = null, edge = '#131927') => {
      const [bx, by, bw, bh] = box;
      const [cx, cy, cw, ch] = crop || [0, 0, meta.width, meta.height];
      const scale = Math.min(bw*X/cw, bh*Y/ch);
      const width = cw*scale, height = ch*scale;
      const x = bx*X + (bw*X-width)/2, y = by*Y + (bh*Y-height)/2;
      const id = `clip-${++app.id}`;
      return `<g filter="url(#shadow)"><rect x="${x-13*U}" y="${y-13*U}" width="${width+26*U}" height="${height+26*U}" rx="${30*U}" fill="${edge}"/>
        <clipPath id="${id}"><rect x="${x}" y="${y}" width="${width}" height="${height}" rx="${20*U}"/></clipPath>
        <g clip-path="url(#${id})"><svg x="${x}" y="${y}" width="${width}" height="${height}" viewBox="${cx} ${cy} ${cw} ${ch}" preserveAspectRatio="xMidYMid meet"><image width="${meta.width}" height="${meta.height}" href="${captures[name]}"/></svg></g></g>`;
    };
    app.id = 0;
    const brand = (light, number) => `<clipPath id="brand-mask"><rect x="${140*X}" y="${80*Y}" width="${80*U}" height="${80*U}" rx="${18*U}"/></clipPath>
      <image x="${140*X}" y="${80*Y}" width="${80*U}" height="${80*U}" href="${icon}" clip-path="url(#brand-mask)"/>` +
      text('Groove', 245, 143, 58, light ? '#ffffff' : '#133a91') +
      text(`${number} / 05`, 2612, 138, 32, light ? '#c8dbff' : '#57709c', 500, 'end', 4);
    const svg = (inside, dark) => `<svg xmlns="http://www.w3.org/2000/svg" width="${W}" height="${H}" viewBox="0 0 ${W} ${H}">
      <defs><linearGradient id="blue" x1="0" y1="0" x2="1" y2="1"><stop stop-color="#1645c7"/><stop offset="1" stop-color="#06286f"/></linearGradient>
      <linearGradient id="pale" x1="0" y1="0" x2="1" y2="1"><stop stop-color="#f6f9ff"/><stop offset="1" stop-color="#dceaff"/></linearGradient>
      <filter id="shadow" x="-25%" y="-25%" width="150%" height="170%"><feDropShadow dx="0" dy="${28*U}" stdDeviation="${30*U}" flood-color="#00154c" flood-opacity=".28"/></filter></defs>
      <rect width="${W}" height="${H}" fill="${dark ? 'url(#blue)' : 'url(#pale)'}"/>
      <circle cx="${2650*X}" cy="${1700*Y}" r="${850*U}" fill="${dark ? '#3270ef' : '#c5d9ff'}" opacity=".20"/>
      <circle cx="${250*X}" cy="${2400*Y}" r="${950*U}" fill="${dark ? '#235fdd' : '#ffffff'}" opacity=".25"/>
      ${inside}</svg>`;

    const frames = [
      svg(brand(true, '01') + text('Make great music.', 1376, 360, 186, '#ffffff', 700, 'middle', -6) +
        text('Start with a beat. Make it a song.', 1376, 492, 60, '#d9e7ff', 500, 'middle') +
        app('skyline', [160, 620, 2432, 1335]), true),
      svg(brand(false, '02') + lines(['Find your', 'sound.'], 140, 405, 170, '#103a92', 195) +
        lines(['Drums. Bass. Synths.', 'Vocals. And more.'], 150, 765, 64, '#445c85', 95, 500) +
        app('skyline', [1250, 360, 1345, 850]) + pill('THE SOUND LIBRARY · DETAIL', 150, 1200, '#315692') +
        app('skyline', [150, 1290, 2452, 610], [0, meta.height*960/1520, meta.width, meta.height*560/1520]), false),
      svg(brand(true, '03') + lines(['Layer your ideas.', 'Find your groove.'], 1376, 370, 158, '#ffffff', 185, 700, 'middle') +
        text('Build your arrangement across eight tracks.', 1376, 695, 60, '#d9e7ff', 500, 'middle') +
        app('afterglow', [150, 850, 2452, 985], [0, 0, meta.width, meta.height*950/1520]) +
        text('YOUR ARRANGEMENT. YOUR WAY.', 1376, 1980, 35, '#c8dbff', 600, 'middle', 6), true),
      svg(brand(false, '04') + lines(['A head start.', 'Your own finish.'], 1376, 365, 160, '#103a92', 185, 700, 'middle') +
        text('Explore three complete demos. Edit every block.', 1376, 690, 58, '#445c85', 500, 'middle') +
        names.map((name, i) => pill(`0${i+1}`, 145+i*835, 890, '#315692') +
          app(name, [145+i*835, 965, 790, 555]) +
          text(['Neon Skyline','Afterglow Avenue','Victory Lights'][i], 540+i*835, 1630, 62, '#103a92', 700, 'middle', -2)).join('') +
        `<rect y="${1790*Y}" width="${W}" height="${274*Y}" fill="#113b97"/>` +
        text('A new idea is only a play button away.', 1376, 1955, 68, '#ffffff', 600, 'middle', -2), false),
      svg(brand(true, '05') + text('Keep the spark.', 1376, 360, 186, '#ffffff', 700, 'middle', -6) +
        text('Save your song. Come back inspired.', 1376, 492, 60, '#d9e7ff', 500, 'middle') +
        app('victory', [160, 620, 2432, 1335]), true),
    ];
    for (let i = 0; i < frames.length; i++) {
      const file = path.join(out, slides[i][0] + '.jpg');
      await sharp(Buffer.from(frames[i])).flatten({background: '#ffffff'}).removeAlpha()
        .jpeg({quality: 90, chromaSubsampling: '4:4:4', mozjpeg: true}).toFile(file);
    }
    const thumbW = 688, thumbH = Math.round(thumbW*H/W);
    const thumbs = await Promise.all(slides.map(([stem]) => sharp(path.join(out, stem+'.jpg')).resize(thumbW,thumbH).png().toBuffer()));
    await sharp({create:{width:thumbW*3+64, height:thumbH*2+48, channels:3, background:'#e7edf7'}})
      .composite(thumbs.map((input,i)=>({input,left:16+(i%3)*(thumbW+16),top:16+Math.floor(i/3)*(thumbH+16)})))
      .jpeg({quality:92}).toFile(path.join(root, `${set.name}-overview.jpg`));
    manifest.sets.push({...set, screenshots: slides.map(([stem,title])=>({file:`${set.name}/en-US/${stem}.jpg`,title}))});
  }
  await fs.writeFile(path.join(root,'manifest.json'), JSON.stringify(manifest,null,2)+'\n');
  console.log('Rendered five macOS screenshots and five iPad draft layouts.');
}
main().catch(e=>{console.error(e);process.exitCode=1;});
