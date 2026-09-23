import fs from 'fs';
import path from 'path';

function walk(dir) {
  const files = fs.readdirSync(dir);
  for (const file of files) {
    const fullPath = path.join(dir, file);
    if (fs.statSync(fullPath).isDirectory()) {
      walk(fullPath);
    } else if (fullPath.endsWith('.tsx') || fullPath.endsWith('.ts')) {
      let content = fs.readFileSync(fullPath, 'utf-8');
      let changed = false;
      
      // Fix InformationCircleIcon missing in ConfirmDialog and LogDialog
      if (content.includes('InformationCircleIcon')) {
        content = content.replace(/InformationCircleIcon/g, 'Info');
        if (!content.includes('Info } from "lucide-react"')) {
            content = content.replace(/\} from "lucide-react"/, ', Info } from "lucide-react"');
        }
        changed = true;
      }
      
      // Remove Lu prefix from lucide-react imports and usages
      if (content.includes('from "lucide-react"')) {
        const importMatch = content.match(/import\s+\{([^}]+)\}\s+from\s+"lucide-react"/);
        if (importMatch) {
          const imports = importMatch[1].split(',').map(i => i.trim());
          const newImports = imports.map(i => {
            if (i.startsWith('Lu') && i !== 'LucideIcon') {
              const newName = i.substring(2);
              content = content.replace(new RegExp(`\\b${i}\\b`, 'g'), newName);
              return newName;
            }
            return i;
          });
          changed = true;
        }
      }
      
      if (changed) {
        fs.writeFileSync(fullPath, content);
      }
    }
  }
}

walk('./src');
